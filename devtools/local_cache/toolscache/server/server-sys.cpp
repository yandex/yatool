#include "server.h"

#include <devtools/local_cache/common/logger-utils/fallback_logger.h>
#include <devtools/local_cache/common/server-utils/server.h>
#include <devtools/local_cache/psingleton/server/server.h>
#include <devtools/local_cache/psingleton/systemptr.h>

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/logger/null.h>
#include <library/cpp/logger/rotating_file.h>
#include <library/cpp/logger/stream.h>

#include <library/cpp/sqlite3/sqlite.h>
#include <library/cpp/svnversion/svnversion.h>

#include <util/folder/dirut.h>
#include <util/system/env.h>

namespace {
    struct TOptions : NToolsCache::TConfigOptions, NToolsCache::TClientOptions {
        TString IniFile = "tc.ini";
        bool Daemonize = false;
        bool ForcedSeizure = false;
        bool Halt = false;
        bool NoLogs = false;
    };
}

using namespace NConfig;
using namespace NToolsCache;
using namespace NToolsCachePrivate;
using namespace NUserService;

static void SetLogBackend(const TOptions& opts, const NConfig::TConfig& config, TLog& log) {
    THolder<TLogBackend> logBE; // owned by logger
    auto logName = ::GetLogName(config);

    if (!opts.Daemonize && (logName.Empty() || opts.Halt || opts.Verbose)) {
        logBE = MakeHolder<TStreamLogBackend>(&Cerr);
    } else {
        if (opts.NoLogs || logName.Empty()) {
            logBE = MakeHolder<TNullLogBackend>();
        } else {
            auto fallbackLogName = JoinFsPaths(::GetPersistentDirectory(config), "fallback.log");
            logBE = MakeHolder<TLogBackendWithFallback>(
                new TRotatingFileLogBackend(logName, 65000, 1),
                new TRotatingFileLogBackend(fallbackLogName, 4096, 1));
        }
    }
    log.ResetBackend(std::move(logBE) /*pass ownership*/);
}

static int StartServer(const TOptions& opts, const NConfig::TConfig& config, TLog& log) {
    TAutoPtr<NCachesPrivate::IParentChildChannel> ptr;
    // Processing in child if service started
    EMode mode = opts.Halt
                     ? Halt
                     : (opts.ForcedSeizure
                            ? ForcedSeizure
                            : Normal);
#if defined(_unix_)
    if (opts.Daemonize && mode != Halt) {
        ptr = NCachesPrivate::Daemonize(log);
    }
#endif
    auto rc = ServerCode(mode, config, log, ptr.Get());
    if (rc) {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCSERV]") << "Server exited with rc: " << rc << Endl;
    } else {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Server stopped gracefully" << Endl;
    }
    return rc;
}

int main(int argc, const char* argv[]) {
    TOptions opts;
    NLastGetopt::TOpts parser;

    /// Server's options.
    parser.AddLongOption('d', "daemonize", "daemonize before processing").NoArgument().DefaultValue(false).StoreResult(&opts.Daemonize, true);
    parser.AddLongOption('f', "force-seizure", "force seizure of server").NoArgument().DefaultValue(false).StoreResult(&opts.ForcedSeizure, true);
    parser.AddLongOption('h', "halt", "stop existing server and exit").NoArgument().DefaultValue(false).StoreResult(&opts.Halt, true);
    parser.AddLongOption('b', "no-logs", "no-log mode").Hidden().NoArgument().DefaultValue(false).StoreResult(&opts.NoLogs, true);
    parser.AddLongOption('i', "ini-file", ".ini file name").DefaultValue("tc.ini").StoreResult(&opts.IniFile);
    parser.AddLongOption('v', "verbose", "").NoArgument().DefaultValue(false).StoreResult(&opts.Verbose, true);

    /// Clients options.
    parser.AddLongOption('D', "deadline", "deadline for client connections").StoreResult(&opts.Deadline);
    parser.AddLongOption("force-tc-gc", "force tools cache gc").StoreResultT<i64>(&opts.ForceTCGC);
    parser.AddLongOption("force-ac-gc", "force build cache gc").StoreResultT<i64>(&opts.ForceACGC);
    parser.AddLongOption('L', "lock-resource", "lock resource").StoreResultT<TString>(&opts.LockResource);
    parser.AddLongOption('U', "unlock-resource", "unlock resource").StoreResultT<TString>(&opts.UnlockSBResource);
    parser.AddLongOption("unlock-all-resources", "unlock all resources").NoArgument().DefaultValue(false).StoreResult(&opts.UnlockAllResources, true);
    parser.AddLongOption('n', "read-only", "set processing in read-only mode").Hidden().NoArgument().DefaultValue(false).StoreResult(&opts.Readonly, true);
    parser.AddLongOption('r', "resume", "resume DB processing").Hidden().NoArgument().DefaultValue(false).StoreResult(&opts.Resume, true);
    parser.AddLongOption('s', "suspend", "suspend DB processing").Hidden().NoArgument().DefaultValue(false).StoreResult(&opts.Suspend, true);
    parser.AddLongOption('w', "read-write", "set processing in read-write mode").Hidden().NoArgument().DefaultValue(false).StoreResult(&opts.Readwrite, true);

    parser.AddHelpOption('?');
    opts.GrpcParams.AddOptions(parser);
    opts.ServerParams.AddOptions(parser);
    opts.TcParams.AddOptions(parser);
    opts.AcParams.AddOptions(parser);

    // Argument as comment.
    parser.SetFreeArgsMax(1);

    TMaybe<TString> commentArgument;

    {
        parser.AllowUnknownLongOptions_ = true;
        parser.AllowUnknownCharOptions_ = true;
        NLastGetopt::TOptsParseResult result{&parser, argc, argv};
        const auto& args = result.GetFreeArgs();
        if (args.size()) {
            commentArgument = args[0];
        }
    }

    if (opts.TcParams.Version.Empty()) {
        opts.TcParams.Version = GetArcadiaLastChangeNum();
        // NO_SVN_DEPENDS returns -1.
        opts.TcParams.Version = opts.TcParams.Version < 0 ? 0 : opts.TcParams.Version;
    }

    try {
        NFs::SetCurrentWorkingDirectory(GetHomeDir());
    } catch (const TSystemError& ex) {
        if (TFsPath(GetHomeDir()).Exists()) {
            Cerr << "Cannot change current directory to home (" << ex.what() << "), ignoring" << Endl;
        }
    }

    NConfig::TConfig config;
    try {
        config = ReadConfig(opts.IniFile, opts);

        NUserServicePrivate::CheckConfig(config);
        NToolsCacheServerPrivate::CheckConfig(config);
        NToolsCachePrivate::CheckConfig(config);
        NACCachePrivate::CheckConfig(config);

        NUserServicePrivate::PrepareDirs(config);
        NToolsCachePrivate::PrepareDirs(config);
        NACCachePrivate::PrepareDirs(config);

        // Need persistent temp directory for local socket.
        SetEnv("TMPDIR", ::GetPersistentDirectory(config));
        SetEnv("TMP", ::GetPersistentDirectory(config));
    } catch (const yexception& ex) {
        TLog log;
        log.ResetBackend(MakeHolder<TStreamLogBackend>(&Cerr));
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "ERR[TEST]") << "Got exception: " << ex.what() << Endl;
        throw ex;
    }

    TLog log;
    SetLogBackend(opts, config, log);

    if (commentArgument) {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]")
            << "Free comment argument: '" << commentArgument.GetRef() << "'" << Endl;
    }

    try {
        if (opts.IsClient()) {
            // return immediately in client mode.
            return ClientCode(opts, config, log);
        }
    } catch (const TGrpcException& ex) {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCSERV]") << "Exception during RPC, will retry: " << ex.what() << Endl;
        // Start server first.
        opts.Daemonize = true;
        opts.Halt = false;
        SetLogBackend(opts, config, log);
    }

    TLog cerrLog(MakeHolder<TStreamLogBackend>(&Cerr));

    int rc = 0;
    try {
        rc = StartServer(opts, config, log);
    } catch (const NCachesPrivate::TServerStop& stopping) {
        // From parent of daemon
        // Another attempt to contact server.
        if (stopping.ExitCode == 0 && opts.IsClient()) {
            opts.Daemonize = false;
            SetLogBackend(opts, config, log);
            return ClientCode(opts, config, log);
        }
        rc = stopping.ExitCode;
    } catch (const std::exception& e) {
        rc = NUserService::ClassifyException(e);
        if (rc == NoMemEC) {
            Cerr << "Exception caught: " << e.what() << Endl;
        } else if (rc == NoSpcEC) {
            LOGGER_CHECKED_GENERIC_LOG(cerrLog, TRTYLogPreprocessor, TLOG_EMERG, "EMERG[TCSERV]") << "Exception caught: " << e.what() << Endl;
        } else {
            for (auto* l : {&cerrLog, &log}) {
                LOGGER_CHECKED_GENERIC_LOG(*l, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCSERV]") << "Exception caught: " << e.what() << Endl;
            }
        }
    }

    if (EqualToOneOf(rc, IOEC, CriticalErrorHandlerEC, ExternalErrorEC)) {
        if (auto marker = GetCriticalErrorMarkerFileName(config); !marker.Empty()) {
            try {
                TStringBuf buf("");
                TFile(marker, OpenAlways | WrOnly | AWUser | ARUser).Write(buf.begin(), buf.Size());
            } catch (...) {
                // Post-pone till next failure ...
            }
        }
    }

    return rc;
}
