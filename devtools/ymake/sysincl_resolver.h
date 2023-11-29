#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/symbols/name_store.h>

#include <library/cpp/regex/pcre/regexp.h>

#include <util/generic/hash.h>
#include <util/generic/hash_multi_map.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>

struct TSysinclRules {
    struct TRule {
        TString Include;
        bool CaseSensitive = true;
        TString SrcFilter;
        TVector<TString> Targets;
    };

    THashMultiMap<TString, TRule> Headers;
    THashMap<TString, TRegExMatch> Filters;
};

/// @brief Provides global-configured (via SYSINCL variable) include resolving
class TSysinclResolver {
public:
    using TResult = const TVector<TString>*;

    TSysinclResolver() = default;
    TSysinclResolver(TSysinclRules&& rules)
        : Rules(rules)
    {
    }

    TSysinclResolver operator=(TSysinclRules&& rules) {
        Rules = rules;
        return *this;
    }

    /// @brief resolves single include into zero, single or multiple targets
    ///
    /// Possible return values:
    /// * !result - resolver knows nothing about this include
    /// * result.size() == 0 - this include should not be resolved (system include)
    /// * result.size() >= 1 - resolve this include into result contents
    ///
    /// result.size() > 1 is possible when exact resolve target
    /// is context dependent, e.g. in multimodules
    ///
    /// Result remains valid until next call to Resolve
    TResult Resolve(TFileView src, TStringBuf include) const;

private:
    TSysinclRules Rules;

    mutable TString Buf;
    mutable TVector<TString> Result;
};
