#pragma once

#include "config.h"

#include <library/cpp/logger/global/global.h>

#include <util/generic/hash_set.h>

namespace NYa {
    class TYaTokenFilter : TLogFormatter {
    public:
        explicit TYaTokenFilter(const TVector<TStringBuf>& args);
        TString operator()(ELogPriority, TStringBuf message) const;
    private:
        THashSet<TString> Replacements_;
    };
}
