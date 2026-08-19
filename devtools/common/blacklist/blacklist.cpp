#include "blacklist.h"

#include <util/digest/numeric.h>
#include <util/folder/pathsplit.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/string/split.h>
#include <util/string/strip.h>
#include <util/system/compat.h>

namespace NBlacklist {
    namespace {
        constexpr size_t BucketCount = 256 * 256;

        Y_FORCE_INLINE size_t ComputeIndex(TStringBuf path) {
            static_assert(sizeof(TStringBuf::value_type) == 1);
            Y_ASSERT(!path.empty());
            size_t index = static_cast<unsigned char>(path[0]);
            if (path.size() > 1 && !TPathSplitUnix::IsPathSep(path[1])) {
                index |= (static_cast<size_t>(static_cast<unsigned char>(path[1])) << 8);
            }
            return index;
        }

        struct TTrieNode {
            THashMap<TString, THolder<TTrieNode>> Children;
            EBlacklistRuleStatus Status = EBlacklistRuleStatus::Unspecified;
            TString RulePath;
            TBlacklistRuleSource RuleSource;
        };

        class TTrieBucket {
        public:
            bool Add(const TPathSplitUnix& components, const TBlacklistRule& rule) {
                auto* node = &Root;
                for (const auto component : components) {
                    auto [it, inserted] = node->Children.emplace(TString(component), nullptr);
                    if (inserted) {
                        it->second = MakeHolder<TTrieNode>();
                    }
                    node = it->second.Get();
                }
                const bool newRule = node->Status == EBlacklistRuleStatus::Unspecified;
                node->Status = rule.Status;
                node->RulePath = rule.Path;
                node->RuleSource = rule.Source;
                return newRule;
            }

            TBlacklistMatch MatchPath(const TPathSplitUnix& components) const {
                const auto* node = &Root;
                TBlacklistMatch result;
                for (const auto component : components) {
                    const auto it = node->Children.find(component);
                    if (it == node->Children.end()) {
                        break;
                    }
                    node = it->second.Get();
                    if (node->Status != EBlacklistRuleStatus::Unspecified) {
                        result = {node->Status, &node->RulePath, &node->RuleSource};
                    }
                }
                return result;
            }

            void CollectRules(TVector<const TTrieNode*>& rules) const {
                CollectRules(Root, rules);
            }

            void AccumulateStats(TBlacklistStats& stats) const {
                AccumulateStats(Root, 0, stats);
                stats.EstimatedRetainedBytes += sizeof(*this);
                AccumulateRetainedBytes(Root, false, stats.EstimatedRetainedBytes);
            }

            size_t GetLookupSteps(const TPathSplitUnix& components) const {
                const auto* node = &Root;
                size_t steps = 0;
                for (const auto component : components) {
                    ++steps;
                    const auto it = node->Children.find(component);
                    if (it == node->Children.end()) {
                        break;
                    }
                    node = it->second.Get();
                }
                return steps;
            }

        private:
            static void CollectRules(const TTrieNode& node, TVector<const TTrieNode*>& rules) {
                if (node.Status != EBlacklistRuleStatus::Unspecified) {
                    rules.push_back(&node);
                }
                for (const auto& [_, child] : node.Children) {
                    CollectRules(*child, rules);
                }
            }

            static void AccumulateStats(const TTrieNode& node, size_t depth, TBlacklistStats& stats) {
                for (const auto& [_, child] : node.Children) {
                    ++stats.TrieNodeCount;
                    ++stats.TrieEdgeCount;
                    stats.TrieMaxDepth = Max(stats.TrieMaxDepth, depth + 1);
                    AccumulateStats(*child, depth + 1, stats);
                }
            }

            static void AccumulateRetainedBytes(const TTrieNode& node, bool includeNode, size_t& bytes) {
                if (includeNode) {
                    bytes += sizeof(node);
                }
                bytes += node.RulePath.capacity();
                bytes += node.RuleSource.File.capacity();
                bytes += node.RuleSource.RawRule.capacity();
                bytes += node.Children.bucket_count() * sizeof(void*);
                bytes += node.Children.size() * sizeof(THashMap<TString, THolder<TTrieNode>>::value_type);
                for (const auto& [_, child] : node.Children) {
                    AccumulateRetainedBytes(*child, true, bytes);
                }
            }

        private:
            TTrieNode Root;
        };

        class TTrieMatcher {
        public:
            TTrieMatcher()
                : Buckets(BucketCount)
            {
            }

            void Clear() {
                for (auto& bucket : Buckets) {
                    bucket.Reset();
                }
                RuleCount = 0;
                IsValidHash = false;
            }

            bool Empty() const noexcept {
                return RuleCount == 0;
            }

            void Add(const TBlacklistRule& rule) {
                TPathSplitUnix components(rule.Path);
                auto& bucket = Buckets[ComputeIndex(rule.Path)];
                if (!bucket) {
                    bucket = MakeHolder<TTrieBucket>();
                }
                RuleCount += bucket->Add(components, rule);
                IsValidHash = false;
            }

            TBlacklistMatch MatchPath(TStringBuf path) const {
                if (path.empty()) {
                    return {};
                }
                const TPathSplitUnix components(path);
                if (components.IsAbsolute || components.empty()) {
                    return {};
                }
                if (const auto& bucket = Buckets[ComputeIndex(components.front())]; bucket) {
                    return bucket->MatchPath(components);
                }
                return {};
            }

            size_t GetHash() const {
                if (IsValidHash) {
                    return Hash;
                }
                TVector<const TTrieNode*> rules;
                rules.reserve(RuleCount);
                for (const auto& bucket : Buckets) {
                    if (bucket) {
                        bucket->CollectRules(rules);
                    }
                }
                Sort(rules, [](const TTrieNode* lhs, const TTrieNode* rhs) {
                    return lhs->RulePath < rhs->RulePath;
                });
                Hash = 0;
                for (const auto* rule : rules) {
                    Hash = CombineHashes(Hash, ComputeHash(rule->RulePath));
                    if (rule->Status == EBlacklistRuleStatus::NotBlacklisted) {
                        Hash = CombineHashes(Hash, ComputeHash(TStringBuf("!")));
                    }
                }
                IsValidHash = true;
                return Hash;
            }

            TBlacklistStats GetStats() const {
                TBlacklistStats stats;
                stats.RuleCount = RuleCount;
                stats.EstimatedRetainedBytes = sizeof(*this) + Buckets.capacity() * sizeof(Buckets[0]);
                for (const auto& bucket : Buckets) {
                    if (bucket) {
                        ++stats.NonEmptyBucketCount;
                        bucket->AccumulateStats(stats);
                    }
                }
                return stats;
            }

            size_t GetLookupSteps(TStringBuf path) const {
                if (path.empty()) {
                    return 0;
                }
                const TPathSplitUnix components(path);
                if (components.IsAbsolute || components.empty()) {
                    return 0;
                }
                if (const auto& bucket = Buckets[ComputeIndex(components.front())]; bucket) {
                    return bucket->GetLookupSteps(components);
                }
                return 0;
            }

        private:
            TVector<THolder<TTrieBucket>> Buckets;
            size_t RuleCount = 0;
            mutable size_t Hash = 0;
            mutable bool IsValidHash = false;
        };

    }

    class TSvnBlacklist::TImpl {
    public:
        void Clear() {
            Trie.Clear();
            EffectiveRules.clear();
        }

        bool Empty() const noexcept {
            return Trie.Empty();
        }

        const TBlacklistRule* FindEffectiveRule(TStringBuf path) const {
            const auto it = EffectiveRules.find(path);
            return it == EffectiveRules.end() ? nullptr : &it->second;
        }

        void Add(const TBlacklistRule& rule) {
            EffectiveRules[rule.Path] = rule;
            Trie.Add(rule);
        }

        TBlacklistMatch MatchPath(TStringBuf path) const {
            return Trie.MatchPath(path);
        }

        size_t GetHash() const {
            return Trie.GetHash();
        }

        TBlacklistStats GetStats() const {
            auto stats = Trie.GetStats();
            stats.EstimatedRetainedBytes += sizeof(EffectiveRules);
            stats.EstimatedRetainedBytes += EffectiveRules.bucket_count() * sizeof(void*);
            stats.EstimatedRetainedBytes +=
                EffectiveRules.size() * sizeof(THashMap<TString, TBlacklistRule>::value_type);
            for (const auto& [path, rule] : EffectiveRules) {
                stats.EstimatedRetainedBytes += path.capacity();
                stats.EstimatedRetainedBytes += rule.Path.capacity();
                stats.EstimatedRetainedBytes += rule.Source.File.capacity();
                stats.EstimatedRetainedBytes += rule.Source.RawRule.capacity();
            }
            return stats;
        }

        size_t GetLookupStepsForDiagnostics(TStringBuf path) const {
            return Trie.GetLookupSteps(path);
        }

        TTrieMatcher Trie;
        THashMap<TString, TBlacklistRule> EffectiveRules;
    };

    TSvnBlacklist::TSvnBlacklist()
        : Impl(MakeHolder<TImpl>())
    {
    }

    TSvnBlacklist::~TSvnBlacklist() = default;

    void TSvnBlacklist::Clear() {
        Impl->Clear();
    }

    bool TSvnBlacklist::Empty() const noexcept {
        return Impl->Empty();
    }

    TVector<TBlacklistRule> TSvnBlacklist::ParseRules(TStringBuf content, TStringBuf file) {
        TVector<TBlacklistRule> rules;
        THashMap<TString, size_t> ruleIndexes;
        bool hasBlockingError = false;
        auto consumeLine = [this, file, &rules, &ruleIndexes, &hasBlockingError](TStringBuf token, size_t line) {
            const auto commentPos = token.find('#');
            token = StripString(token.Head(commentPos));
            if (token.empty()) {
                return;
            }

            const TString rawRule{token};
            const TBlacklistRuleSource source{TString{file}, line, rawRule};

            EBlacklistRuleStatus status = EBlacklistRuleStatus::Blacklisted;
            if (token[0] == '!') {
                status = EBlacklistRuleStatus::NotBlacklisted;
                token = StripString(token.SubStr(1));
            }
            if (token.empty()) {
                OnParserDiagnostic({
                    EParserErrorKind::InvalidPath,
                    EBlacklistDiagnosticSeverity::Warning,
                    TString{token},
                    EBlacklistRuleStatus::Unspecified,
                    status,
                    {},
                    source,
                });
                return;
            }

            TPathSplitUnix pathSplit(token);
            if (pathSplit.IsAbsolute) {
                OnParserDiagnostic({
                    EParserErrorKind::AbsolutePath,
                    EBlacklistDiagnosticSeverity::Warning,
                    TString{token},
                    EBlacklistRuleStatus::Unspecified,
                    status,
                    {},
                    source,
                });
            } else if (pathSplit.empty() || pathSplit[0] == TStringBuf(".") || pathSplit[0] == TStringBuf("..")) {
                OnParserDiagnostic({
                    EParserErrorKind::InvalidPath,
                    EBlacklistDiagnosticSeverity::Warning,
                    TString{token},
                    EBlacklistRuleStatus::Unspecified,
                    status,
                    {},
                    source,
                });
            } else {
                const TString path = pathSplit.Reconstruct();
                if (path.empty()) {
                    OnParserDiagnostic({
                        EParserErrorKind::InvalidPath,
                        EBlacklistDiagnosticSeverity::Warning,
                        TString{token},
                        EBlacklistRuleStatus::Unspecified,
                        status,
                        {},
                        source,
                    });
                } else {
                    const TBlacklistRule current{path, status, source};
                    auto [it, inserted] = ruleIndexes.emplace(path, rules.size());
                    if (inserted) {
                        rules.push_back(current);
                    } else {
                        auto& previous = rules[it->second];
                        const bool conflict = previous.Status != current.Status;
                        OnParserDiagnostic({
                            conflict ? EParserErrorKind::ConflictingRule : EParserErrorKind::DuplicateRule,
                            conflict ? EBlacklistDiagnosticSeverity::Error : EBlacklistDiagnosticSeverity::Warning,
                            path,
                            previous.Status,
                            current.Status,
                            previous.Source,
                            current.Source,
                        });
                        hasBlockingError |= conflict;
                        if (!conflict) {
                            previous = current;
                        }
                    }
                }
            }
        };

        size_t offset = 0;
        size_t line = 1;
        while (offset < content.size()) {
            const auto cr = content.find('\r', offset);
            const auto lf = content.find('\n', offset);
            size_t end = content.size();
            if (cr != TStringBuf::npos) {
                end = cr;
            }
            if (lf != TStringBuf::npos && lf < end) {
                end = lf;
            }
            consumeLine(content.SubStr(offset, end - offset), line);
            if (end == content.size()) {
                break;
            }
            offset = end + 1;
            if (content[end] == '\r' && offset < content.size() && content[offset] == '\n') {
                ++offset;
            }
            ++line;
        }
        return hasBlockingError ? TVector<TBlacklistRule>{} : rules;
    }

    void TSvnBlacklist::LoadRules(const TVector<TBlacklistRule>& rules) {
        for (const auto& rule : rules) {
            if (const auto* previous = Impl->FindEffectiveRule(rule.Path)) {
                OnParserDiagnostic({
                    previous->Status == rule.Status ? EParserErrorKind::DuplicateRule : EParserErrorKind::ConflictingRule,
                    EBlacklistDiagnosticSeverity::Warning,
                    rule.Path,
                    previous->Status,
                    rule.Status,
                    previous->Source,
                    rule.Source,
                });
            }
            Impl->Add(rule);
        }
    }

    void TSvnBlacklist::LoadFromString(TStringBuf content, TStringBuf file) {
        LoadRules(ParseRules(content, file));
    }

    TBlacklistMatch TSvnBlacklist::MatchPath(TStringBuf path) const {
        return Impl->MatchPath(path);
    }

    const TString* TSvnBlacklist::IsValidPath(TStringBuf path) const {
        const auto result = MatchPath(path);
        return result.Status == EBlacklistRuleStatus::Blacklisted ? result.RulePath : nullptr;
    }

    size_t TSvnBlacklist::GetHash() const {
        return Impl->GetHash();
    }

    TBlacklistStats TSvnBlacklist::GetStats() const {
        return Impl->GetStats();
    }

    size_t TSvnBlacklist::GetLookupStepsForDiagnostics(TStringBuf path) const {
        return Impl->GetLookupStepsForDiagnostics(path);
    }

    void TSvnBlacklist::OnParserDiagnostic(const TBlacklistDiagnostic& diagnostic) {
        if (diagnostic.Kind == EParserErrorKind::AbsolutePath || diagnostic.Kind == EParserErrorKind::InvalidPath) {
            OnParserError(diagnostic.Kind, diagnostic.Path, diagnostic.CurrentSource.File);
        }
    }

    void TSvnBlacklist::OnParserError(EParserErrorKind, TStringBuf, TStringBuf) {
        // Default implementation: do nothing
    }

} // NBlacklist
