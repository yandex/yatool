#pragma once

#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NBlacklist {

    enum class EParserErrorKind {
        Ok = 0,
        AbsolutePath,
        InvalidPath,
        DuplicateRule,
        ConflictingRule,
    };

    enum class EBlacklistDiagnosticSeverity {
        Warning,
        Error,
    };

    enum class EBlacklistRuleStatus {
        Unspecified,
        Blacklisted,
        NotBlacklisted,
    };

    struct TBlacklistRuleSource {
        TString File;
        size_t Line = 0;
        TString RawRule;
    };

    struct TBlacklistRule {
        TString Path;
        EBlacklistRuleStatus Status = EBlacklistRuleStatus::Blacklisted;
        TBlacklistRuleSource Source;
    };

    struct TBlacklistDiagnostic {
        EParserErrorKind Kind = EParserErrorKind::Ok;
        EBlacklistDiagnosticSeverity Severity = EBlacklistDiagnosticSeverity::Warning;
        TString Path;
        EBlacklistRuleStatus PreviousStatus = EBlacklistRuleStatus::Unspecified;
        EBlacklistRuleStatus CurrentStatus = EBlacklistRuleStatus::Unspecified;
        TBlacklistRuleSource PreviousSource;
        TBlacklistRuleSource CurrentSource;
    };

    struct TBlacklistMatch {
        EBlacklistRuleStatus Status = EBlacklistRuleStatus::Unspecified;
        const TString* RulePath = nullptr;
        const TBlacklistRuleSource* RuleSource = nullptr;
    };

    struct TBlacklistStats {
        size_t RuleCount = 0;
        size_t NonEmptyBucketCount = 0;
        size_t TrieNodeCount = 0;
        size_t TrieEdgeCount = 0;
        size_t TrieMaxDepth = 0;
        size_t EstimatedRetainedBytes = 0;
    };

    class TSvnBlacklist {
    public:
        TSvnBlacklist();
        virtual ~TSvnBlacklist();

        void Clear();
        bool Empty() const noexcept;
        TVector<TBlacklistRule> ParseRules(TStringBuf content, TStringBuf file);
        void LoadRules(const TVector<TBlacklistRule>& rules);
        void LoadFromString(TStringBuf content, TStringBuf file);
        TBlacklistMatch MatchPath(TStringBuf path) const;
        const TString* IsValidPath(TStringBuf path) const;
        size_t GetHash() const;
        TBlacklistStats GetStats() const;
        size_t GetLookupStepsForDiagnostics(TStringBuf path) const;
        virtual void OnParserDiagnostic(const TBlacklistDiagnostic& diagnostic);
        virtual void OnParserError(EParserErrorKind kind, TStringBuf path, TStringBuf file);

    private:
        class TImpl;
        THolder<TImpl> Impl;
    };

} // NBlacklist
