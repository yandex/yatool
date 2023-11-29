#pragma once

#include <devtools/ya/cpp/lib/config.h>

#include <library/cpp/logger/global/global.h>

namespace NYa {
    void InitLogger(const TFsPath& miscRoot, const TVector<TStringBuf>& args, ELogPriority priority = TLOG_INFO, bool verbose = false);
    void InitNullLogger();
    TLog& GetLog();
}
