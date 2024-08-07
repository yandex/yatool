#pragma once

#include "config_conditions.h"

#include <devtools/ymake/diag/dbg.h>

#include <util/generic/string.h>

namespace NConfReader {
    class TConfigError: public TMakeError {
    };

    void UpdateConfMd5(TStringBuf content, MD5& confData);
    TString CalculateConfMd5(TStringBuf content, TString* ignoredHashContent = nullptr);
}
