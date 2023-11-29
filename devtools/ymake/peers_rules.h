#pragma once

#include "path_matcher.h"

#include <devtools/ymake/common/uniq_vector.h>

#include <devtools/ymake/diag/diag.h>

#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <util/stream/input.h>
#include <util/stream/file.h>

class TPeersRules {
public:
    enum class EPolicy {
        ALLOW,
        DENY,
        DEFAULT = DENY
    };

    struct TWildcard {
        IPathMatcher::TRef Matcher;
        bool MatchAll;
    };

    struct TRule {
        TWildcard From;
        TWildcard To;
        EPolicy Policy = EPolicy::DENY;
    };

    const static constexpr EPolicy Default = EPolicy::ALLOW;

protected:
    TVector<TRule> Rules;

    TVector<size_t> GlobalRulesFrom;
    TVector<size_t> GlobalRulesTo;

    friend class TAppliedPeersRules;

public:
    TPeersRules() = default;
    TPeersRules(TVector<TRule>&& rules)
        : Rules(std::move(rules))
    {}

    TPeersRules& operator+=(TPeersRules&& other);

    void Finalize();

    EPolicy GetRuleResult(size_t number) const {
        return Rules.at(number).Policy;
    }

    bool Empty() const {
        return Rules.empty();
    }
};

class TPeersRulesSavedState {
private:
    TVector<size_t> MatchesTo;
    TVector<size_t> MatchesFrom;

    friend class TAppliedPeersRules;

public:
    Y_SAVELOAD_DEFINE(MatchesTo, MatchesFrom);
};

class TAppliedPeersRules {
private:
    const TPeersRules& Rules;
    TVector<size_t> MatchesTo;
    TVector<size_t> MatchesFrom;

public:
    TAppliedPeersRules(const TPeersRules& rules, TStringBuf dirName);
    TAppliedPeersRules(const TPeersRules& rules, TPeersRulesSavedState&& saved)
        : Rules(rules), MatchesTo(std::move(saved.MatchesTo)), MatchesFrom(std::move(saved.MatchesFrom))
    {}

    void Save(TPeersRulesSavedState& saved) const {
        saved.MatchesTo = MatchesTo;
        saved.MatchesFrom = MatchesFrom;
    }

    TPeersRules::EPolicy operator()(const TAppliedPeersRules& child) const;
};

// $S/build/peerdirs.policy syntax:
//
// # This is a comment
// DENY .* -> .*boost.*
// ALLOW sth -> .*boost.*
//
// # Default policy: DENY
// .* -> .*deprecated.*

class TPeersRulesReader {
private:
    TString Filename;
    TAutoPtr<IInputStream> Input;
    size_t LineNumber = 0;
    TVector<TPeersRules::TRule> Rules;

public:
    TPeersRulesReader(IInputStream* input, TString inputName = "")
        : Filename(std::move(inputName)), Input(input)
    {}

    TPeersRules Read();

private:
    bool ParseLine(const TStringBuf& line);
};

namespace NPeers {
    TMaybe<size_t> FindFirstMatch(const TVector<size_t>& localFrom, const TVector<size_t>& localTo, const TVector<size_t>& globalFrom, const TVector<size_t>& globalTo);
}
