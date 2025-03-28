#include "server.h"

#include "devtools/local_cache/toolscache/service/service.h"
#include "devtools/local_cache/ac/service/service.h"

#include "devtools/local_cache/psingleton/server/server.h"
#include "devtools/local_cache/psingleton/server/server-nix.h"

#include "devtools/local_cache/common/server-utils/server.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/yexception.h>
#include <util/system/fs.h>

#include <thread>

using namespace grpc;
using namespace NConfig;

namespace {
    constexpr const char FailureMessage[] = "Failure: Problem starting server";
    constexpr const char ToolServiceName[] = "NToolsCache.TToolsCache";
    constexpr const char ACServiceName[] = "NACCache.TACCache";
}

// Throws TServerStop on exit from service
int NToolsCache::ServerCode(EMode mode, const NConfig::TConfig& config, TLog& log, NCachesPrivate::IParentChildChannel* channel) {
    THolder<NCachesPrivate::IParentChildChannel, NCachesPrivate::TNotifyParent<FailureMessage, static_cast<int>(NUserService::InstallServerGuardEC)>> failureNotification(channel);

    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Preparing" << Endl;

    using namespace NUserService;
    auto notifyParent = [&failureNotification, &log, channel](const TString& msg, int rc) -> void {
        if (!failureNotification.Get()) {
            // Already done.
            return;
        }
        (void)failureNotification.Release();
        int res = 0;
        if ((res = channel->SendToPeer(msg)) != 0) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCSERV]") << "Failed to send message '" << msg << "' to parent, err: " << -res << Endl;
        }
        if ((res = channel->SendToPeer(ToString(rc))) != 0) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCSERV]") << "Failed to send message '" << rc << "' to parent, err: " << -res << Endl;
        }
    };

    TSingletonServer server(config, log);
    if (NToolsCacheServerPrivate::DoRunService(config, NToolsCacheServerPrivate::RunTCStr)) {
        server.AddService<TToolsCacheServer, ToolServiceName>();
    }

    if (NToolsCacheServerPrivate::DoRunService(config, NToolsCacheServerPrivate::RunACStr)) {
        server.AddService<NACCache::TACCacheServer, ACServiceName>();
    }

    using TType = TPSingleton<TFileReadWriteLock<TSystemWideName>, TSystemWideName>;

    TType::TServerStart setupServer =
        [&server, mode](TSystemWideName stale, std::function<void(TSystemWideName)> install) -> TSystemWideName {
        return mode == Halt
                   ? server.HardStop(stale, install)
                   : server.InstallServer(stale, install);
    };

    TType::TServerStop stopServer =
        [&server](TSystemWideName stale, std::function<void(TSystemWideName)> install) -> TSystemWideName {
        return server.Cleanup(stale, install);
    };

    auto lockFile = NToolsCacheServerPrivate::GetServerLockName(config);

    int rc = 0;
    TStringStream sstr;
    try {
        if (!NToolsCacheServerPrivate::DoRunService(config, NToolsCacheServerPrivate::RunTCStr) && mode != Halt) {
            NotifyOtherTC(config, 0, log);
        }

        TType* singleton = Singleton<TType>(lockFile, setupServer, stopServer, log);

        if (mode != Normal) {
            (void)singleton->StartMaintenance(Blocking, mode == ForcedSeizure ? CheckAlive : NoCheckAlive);
        } else {
            (void)singleton->GetInstanceNameForOwnership(Blocking);
        }

        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Server got singleton" << Endl;
        Y_ENSURE_EX(mode == Normal || singleton->IsCaptured(), TWithBackTrace<yexception>());

#if defined(_unix_)
        TTermHandlers signalHandlers(&server, log);
#endif

        if (channel) {
            TStringStream sstr;
            if (auto rc = server.GetExitCode()) {
                sstr << FailureMessage << ", rc=" << rc;
            } else if (singleton->IsCaptured()) {
                auto name = singleton->GetInstanceName(Blocking);
                Y_ENSURE_EX(!name.Empty(), TWithBackTrace<yexception>());
                sstr << "Success: Started " << name.GetRef().ToPlainString();
            } else {
                sstr << "Success: Conceded";
            }
            notifyParent(sstr.Str(), static_cast<int>(0));
        }

        if (singleton->IsCaptured()) {
            THolder<TShutdownPoller> poller;
            if (mode != Halt) {
                poller.Reset(new TShutdownPoller(server, log, lockFile));
                poller->Initialize();
            }
            server.WaitForCompletion();
            (void)singleton->StopLocked(NonBlocking);
        } else {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Conceded" << Endl;
        }
    } catch (const TGrpcException& e) {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCSERV]") << "Could notify other tools cache" << Endl;
        rc = PreconditionEC;
    } catch (const std::exception& e) {
        rc = ClassifyException(e);
        if (rc != NoMemEC && rc != NoSpcEC) {
            sstr << "Exception caught: " << e.what();
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCSERV]") << "Exception caught: " << e.what() << Endl;
        }
    }
    if (rc == 0) {
        rc = server.GetExitCode();
    }
    if (rc) {
        if (rc != NoMemEC && rc != NoSpcEC) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCSERV]") << "Server exited with rc: " << rc << Endl;
        }
    } else {
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Server stopped gracefully" << Endl;
    }
    if (channel) {
        sstr << FailureMessage << ", rc=" << rc;
        // Send info if possible
        notifyParent(sstr.Str(), rc);
    }
    return rc;
}
