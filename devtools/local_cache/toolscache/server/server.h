#pragma once

#include "config.h"

#include "devtools/local_cache/toolscache/service/config.h"

#include "devtools/local_cache/ac/service/config.h"

#include "devtools/local_cache/psingleton/server/config.h"

#include "devtools/local_cache/common/server-utils/server.h"

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/maybe.h>

namespace NToolsCache {
    enum EMode {
        Normal,
        ForcedSeizure,
        Halt
    };

    /// Options overriding values in ini-file.
    struct TConfigOptions {
        NUserServicePrivate::TConfigOptions GrpcParams;
        NToolsCachePrivate::TConfigOptions TcParams;
        NACCachePrivate::TConfigOptions AcParams;
        NToolsCacheServerPrivate::TConfigOptions ServerParams;
    };

    int ServerCode(EMode mode, const NConfig::TConfig& config, TLog& log, NCachesPrivate::IParentChildChannel* channel);

    NConfig::TConfig ReadConfig(TString fileName, const TConfigOptions& opts);

    struct TClientOptions {
        // Client's options
        TMaybe<i64> ForceTCGC;
        TMaybe<i64> ForceACGC;
        TMaybe<TString> LockResource;
        TMaybe<TString> UnlockSBResource;
        i64 Deadline = 0;
        bool Resume = false;
        bool Suspend = false;
        bool Readonly = false;
        bool Readwrite = false;
        bool UnlockAllResources = false;
        bool Verbose = false;

        bool IsClient() const {
            return !ForceTCGC.Empty() || !ForceACGC.Empty() || !LockResource.Empty() || !UnlockSBResource.Empty() || Resume || Suspend || Readonly || Readwrite || UnlockAllResources;
        }
    };

    void NotifyOtherTC(const NConfig::TConfig& config, i64 deadline, TLog& log);

    int ClientCode(const TClientOptions& opts, const NConfig::TConfig& config, TLog& log);

    TString GetToolDir();

    TString GetMiscRoot();

    TString GetCacheRoot();
}

// Wrapper to deal with deprecated config-files.
inline TString GetLogName(const NConfig::TConfig& config) {
    auto logName = NUserServicePrivate::GetLogName(config);
    if (logName.Empty()) {
        // deprecated
        logName = NToolsCachePrivate::GetLogName(config);
    }
    return logName;
}

TString GetPersistentDirectory(const NConfig::TConfig& config);

TString GetCriticalErrorMarkerFileName(const NConfig::TConfig& config);
