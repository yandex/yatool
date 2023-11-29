#include "peers_rules.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/regex/pcre/regexp.h>

#include <util/string/cast.h>

#include <limits>

namespace {
    const TRegExBase STATEMENT_PATTERN = "\\s*(?:(\\w*)\\s+)?(\\S+)\\s+->\\s+(\\S+)\\s*";
    const TRegExBase COMMENT_PATTERN = "\\s*(?:#.*)?";
}

namespace NPeers {
    TMaybe<size_t> FindFirstMatch(const TVector<size_t>& localFrom, const TVector<size_t>& localTo, const TVector<size_t>& globalFrom, const TVector<size_t>& globalTo) {
        auto localToIt = localTo.begin();
        auto localFromIt = localFrom.begin();
        auto globalToIt = globalTo.begin();
        auto globalFromIt = globalFrom.begin();

        auto chooseMin = [](auto& first, const auto& firstEnd, auto& second, const auto& secondEnd) -> TVector<size_t>::const_iterator& {
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
    auto result = NPeers::FindFirstMatch(MatchesFrom, child.MatchesTo, Rules.GlobalRulesFrom, Rules.GlobalRulesTo);

    if (result.Empty()) {
        return TPeersRules::Default;
    }

    return Rules.GetRuleResult(*result);
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
    for (size_t i = 0; i < Rules.size(); i++) {
        if (Rules[i].From.MatchAll) {
            GlobalRulesFrom.push_back(i);
        }
        if (Rules[i].To.MatchAll) {
            GlobalRulesTo.push_back(i);
        }
    }

    Rules.shrink_to_fit();
    GlobalRulesFrom.shrink_to_fit();
    GlobalRulesTo.shrink_to_fit();
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

        auto startFromTheBegin = [](const TStringBuf& pattern) {
            const TStringBuf newLineMark = "^";
            return pattern.StartsWith(newLineMark) ? ToString(pattern) : ToString(newLineMark) + pattern;
        };

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

        TString fixedFromPattern = startFromTheBegin(fromPattern);
        TString fixedToPattern = startFromTheBegin(toPattern);

        try {
            TPeersRules::TRule rule;

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
