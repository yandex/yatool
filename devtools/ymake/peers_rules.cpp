#include "peers_rules.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/regex/pcre/regexp.h>

#include <util/generic/hash.h>
#include <util/string/cast.h>
#include <util/string/split.h>
#include <util/system/spinlock.h>

#include <algorithm>
#include <limits>

namespace {
    const TRegExBase STATEMENT_PATTERN = "\\s*(?:(\\w*)\\s+)?(\\S+)\\s+->\\s+(\\S+)\\s*";
    const TRegExBase COMMENT_PATTERN = "\\s*(?:#.*)?";
}

namespace {
    TString AnchorPattern(TStringBuf pattern) {
        return pattern.StartsWith('^') ? TString{pattern} : TString::Join("^", pattern);
    }

    TMaybe<size_t> FindFirstMatch(TConstArrayRef<size_t> localFrom, TConstArrayRef<size_t> localTo, TConstArrayRef<size_t> globalFrom, TConstArrayRef<size_t> globalTo) {
        auto localToIt = localTo.begin();
        auto localFromIt = localFrom.begin();
        auto globalToIt = globalTo.begin();
        auto globalFromIt = globalFrom.begin();

        auto chooseMin = [](auto& first, const auto& firstEnd, auto& second, const auto& secondEnd) -> auto& {
            if (first == firstEnd) {
                return second;
            } else if (second == secondEnd) {
                return first;
            } else {
                return *first < *second ? first : second;
            }
        };

        while ((localToIt != localTo.end() || globalToIt != globalTo.end()) && (localFromIt != localFrom.end() || globalFromIt != globalFrom.end())) {
            auto& to = chooseMin(localToIt, localTo.end(), globalToIt, globalTo.end());
            auto& from = chooseMin(localFromIt, localFrom.end(), globalFromIt, globalFrom.end());

            if (*to == *from) {
                return *to;
            } else if (*to < *from) {
                to++;
            } else {
                from++;
            }
        }

        return {};
    }
}

TMaybe<size_t> NPeers::FindFirstMatch(const TVector<size_t>& localFrom, const TVector<size_t>& localTo, const TVector<size_t>& globalFrom, const TVector<size_t>& globalTo) {
    return ::FindFirstMatch(localFrom, localTo, globalFrom, globalTo);
}

TAppliedPeersRules::TAppliedPeersRules(const TPeersRules& rules, TStringBuf dirName)
    : Rules(rules)
{
    dirName = NPath::CutType(dirName);

    for (size_t i = 0; i < Rules.Rules.size(); i++) {
        const auto& rule = Rules.Rules[i];

        if (!rule.From.MatchAll && rule.From.Matcher->Match(dirName)) {
            MatchesFrom.push_back(i);
        }

        if (!rule.To.MatchAll && rule.To.Matcher->Match(dirName)) {
            MatchesTo.push_back(i);
        }
    }
}

TPeersRules::EPolicy TAppliedPeersRules::operator()(const TAppliedPeersRules& child) const {
    if (const auto result = MatchPriority(child)) {
        return *result;
    }
    return MatchGlobal(child, false).GetOrElse(TPeersRules::Default);
}

TConstArrayRef<size_t> TAppliedPeersRules::TierCandidates(const TVector<size_t>& matches, bool priority) const {
    const TConstArrayRef<size_t> all(matches);
    if (matches.empty() || !Rules.HasPriorityRules || !Rules.HasFallbackRules) {
        return all;
    }
    const auto split = std::lower_bound(matches.begin(), matches.end(), Rules.PriorityRulesCount) - matches.begin();
    return priority ? all.Slice(0, split) : all.Slice(split);
}

TMaybe<TPeersRules::EPolicy> TAppliedPeersRules::MatchGlobal(const TAppliedPeersRules& child, bool priority, TPeersRules::ECachedMatch* cached) const {
    using EMatch = TPeersRules::ECachedMatch;
    if (cached && *cached != EMatch::Unknown) {
        switch (*cached) {
            case EMatch::Allow: return TPeersRules::EPolicy::ALLOW;
            case EMatch::Deny: return TPeersRules::EPolicy::DENY;
            case EMatch::NoMatch: return {};
            case EMatch::Unknown: Y_UNREACHABLE();
        }
    }
    if (priority ? !Rules.HasPriorityRules : !Rules.HasFallbackRules) {
        if (cached) {
            *cached = EMatch::NoMatch;
        }
        return {};
    }
    const size_t tier = priority ? 0 : 1;
    const auto result = ::FindFirstMatch(TierCandidates(MatchesFrom, priority), child.TierCandidates(child.MatchesTo, priority),
        Rules.GlobalRulesFrom[tier], Rules.GlobalRulesTo[tier]);
    if (result) {
        const auto policy = Rules.GetRuleResult(*result);
        if (cached) {
            *cached = policy == TPeersRules::EPolicy::ALLOW ? EMatch::Allow : EMatch::Deny;
        }
        return policy;
    }
    if (cached) {
        *cached = EMatch::NoMatch;
    }
    return {};
}

TMaybe<TPeersRules::EPolicy> TAppliedPeersRules::MatchPriority(const TAppliedPeersRules& child, TPeersRules::TMatchCache* cached) const {
    return MatchGlobal(child, true, cached ? &cached->Priority : nullptr);
}

TVector<TAppliedPeersRules::TLocalRule> TAppliedPeersRules::LoadLocalRules(TStringBuf value) const {
    TVector<TStringBuf> tokens;
    StringSplitter(value).Split(' ').SkipEmpty().AddTo(&tokens);
    TVector<TLocalRule> result;
    Y_ASSERT(tokens.size() % 2 == 0);
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        result.push_back({Rules.GetLocalMatcher(tokens[i + 1]), FromString<TPeersRules::EPolicy>(tokens[i])});
    }
    return result;
}

void TAppliedPeersRules::SetLocalRules(TStringBuf fromRules, TStringBuf toRuleOverrides) {
    FromRules = LoadLocalRules(fromRules);
    ToRuleOverrides = LoadLocalRules(toRuleOverrides);
}

TPeersRules::EPolicy TAppliedPeersRules::CheckDirectPeer(const TAppliedPeersRules& child, TStringBuf fromDir, TStringBuf toDir, TPeersRules::TMatchCache* cached) const {
    if (!NeedsDirectPeerCheck(child)) {
        return TPeersRules::Default;
    }
    // Priority rules are enforced at the existing direct/transitive validation
    // sites. A matched priority rule excludes this pair from deferred checks,
    // including when the original validation exempts an implicit peer.
    if (MatchPriority(child, cached)) {
        return TPeersRules::EPolicy::ALLOW;
    }
    for (const auto& rule : ToRuleOverrides) {
        if (rule.Matcher->Match(NPath::CutType(toDir))) {
            return rule.Policy;
        }
    }
    for (const auto& rule : child.FromRules) {
        if (rule.Matcher->Match(NPath::CutType(fromDir))) {
            return rule.Policy;
        }
    }
    return MatchGlobal(child, false, cached ? &cached->Fallback : nullptr).GetOrElse(TPeersRules::Default);
}

struct TPeersRules::TLocalMatcherCache {
    TAdaptiveLock Lock;
    THashMap<TString, IPathMatcher::TRef> Matchers;
};

TPeersRules::TPeersRules()
    : LocalMatchers(std::make_shared<TLocalMatcherCache>())
{}

IPathMatcher::TRef TPeersRules::GetLocalMatcher(TStringBuf pattern) const {
    auto anchored = AnchorPattern(pattern);
    // Cache lifetime follows the configuration, including lazily restored modules.
    // Compilation is serialized so concurrent restores also share the first matcher.
    auto guard = Guard(LocalMatchers->Lock);
    if (const auto it = LocalMatchers->Matchers.find(anchored); it != LocalMatchers->Matchers.end()) {
        return it->second;
    }
    auto matcher = IPathMatcher::Construct(anchored);
    LocalMatchers->Matchers.emplace(std::move(anchored), matcher);
    return matcher;
}

TPeersRules& TPeersRules::operator+=(TPeersRules&& other) {
    if (Rules.empty()) {
        Rules = std::move(other.Rules);
    } else {
        Rules.reserve(Rules.size() + other.Rules.size());
        std::move(other.Rules.begin(), other.Rules.end(), std::back_inserter(Rules));
    }
    return *this;
}

void TPeersRules::Finalize() {
    const auto fallback = std::stable_partition(Rules.begin(), Rules.end(), [](const TRule& rule) { return rule.Priority; });
    PriorityRulesCount = fallback - Rules.begin();
    HasPriorityRules = PriorityRulesCount != 0;
    HasFallbackRules = PriorityRulesCount != Rules.size();
    for (size_t tier = 0; tier < 2; ++tier) {
        GlobalRulesFrom[tier].clear();
        GlobalRulesTo[tier].clear();
    }
    for (size_t i = 0; i < Rules.size(); i++) {
        const size_t tier = Rules[i].Priority ? 0 : 1;
        if (Rules[i].From.MatchAll) {
            GlobalRulesFrom[tier].push_back(i);
        }
        if (Rules[i].To.MatchAll) {
            GlobalRulesTo[tier].push_back(i);
        }
    }

    Rules.shrink_to_fit();
    for (size_t tier = 0; tier < 2; ++tier) {
        GlobalRulesFrom[tier].shrink_to_fit();
        GlobalRulesTo[tier].shrink_to_fit();
    }
}

TPeersRules TPeersRulesReader::Read() {
    TString currentLine;
    size_t result = Input->ReadLine(currentLine);
    LineNumber = 1;

    while (result > 0 && ParseLine(currentLine)) {
        LineNumber++;
        result = Input->ReadLine(currentLine);
    }

    if (result == 0) {
        return TPeersRules(std::move(Rules));
    } else {
        YConfErr(Misconfiguration) << "Syntax error at line " << LineNumber << " in " << Filename << ": " << currentLine << Endl;
        return TPeersRules();
    }
}

bool TPeersRulesReader::ParseLine(const TStringBuf& line) {
    if (line.empty()) {
        return true;
    }

    regmatch_t matches[NMATCHES];

    if (COMMENT_PATTERN.Exec(line.data(), matches, 0) == 0) {
        if (static_cast<size_t>(matches[0].rm_eo - matches[0].rm_so) == line.size()) {
            return true;
        }
    }

    if (STATEMENT_PATTERN.Exec(line.data(), matches, 0) == 0) {
        if (static_cast<size_t>(matches[0].rm_eo - matches[0].rm_so) != line.size()) {
            return false;
        }

        TStringBuf policy(line.data(), static_cast<size_t>(matches[1].rm_so), static_cast<size_t>(matches[1].rm_eo - matches[1].rm_so));
        TStringBuf fromPattern(line.data(), static_cast<size_t>(matches[2].rm_so), static_cast<size_t>(matches[2].rm_eo - matches[2].rm_so));
        TStringBuf toPattern(line.data(), static_cast<size_t>(matches[3].rm_so), static_cast<size_t>(matches[3].rm_eo - matches[3].rm_so));

        auto makeWildcard = [](const TString& pattern) {
            TPeersRules::TWildcard wildcard;
            if (pattern != "^.*") {
                wildcard.Matcher = IPathMatcher::Construct(pattern);
                wildcard.MatchAll = false;
            } else {
                wildcard.MatchAll = true;
            }
            return wildcard;
        };

        TString fixedFromPattern = AnchorPattern(fromPattern);
        TString fixedToPattern = AnchorPattern(toPattern);

        try {
            TPeersRules::TRule rule;

            if (policy == "FALLBACK_ALLOW" || policy == "FALLBACK_DENY") {
                rule.Priority = false;
                policy.SkipPrefix("FALLBACK_");
            } else if (!policy.empty() && policy != "ALLOW" && policy != "DENY" && policy != "DEFAULT") {
                YConfWarn(Misconfiguration) << "line " << LineNumber << ": Unknown peerdir policy '" << policy
                    << "'; expected one of: ALLOW, DENY, DEFAULT, FALLBACK_ALLOW, FALLBACK_DENY";
                return false;
            }
            if (!policy.empty()) {
                rule.Policy = FromString<TPeersRules::EPolicy>(policy);
            }

            rule.From = makeWildcard(fixedFromPattern);
            rule.To = makeWildcard(fixedToPattern);

            Rules.push_back(std::move(rule));

            return true;
        } catch (const yexception& ex) {
            YConfWarn(Misconfiguration) << "line " << LineNumber << ": " << ex.what();
            return false;
        }
    }

    return false;
}
