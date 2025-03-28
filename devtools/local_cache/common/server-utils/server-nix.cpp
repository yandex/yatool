#include "server.h"

#include <devtools/executor/proc_util/proc_util.h>

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/yexception.h>
#include <util/string/cast.h>
#include <util/system/error.h>
#include <util/system/daemon.h>
#include <util/system/sigset.h>

#include <signal.h>

static void SetupDaemon(NCachesPrivate::IParentChildChannel& channel, TLog& log) {
    using namespace NDaemonMaker;

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    SigEmptySet(&sa.sa_mask);
    Y_ENSURE_EX(sigaction(SIGHUP, &sa, nullptr) == 0, TWithBackTrace<yexception>());

    // Almost all FDs are closed.
    if (MakeMeDaemon(closeStdIoOnly, openDevNull, chdirNone, returnFromParent)) {
        channel.CleanupParent();
        log.ReopenLog();
        TString msg;
        TString msgRc;
        auto status = channel.RecvFromPeer(msg);
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[DAEM]") << "Message from server: " << msg << ", status: " << -status << Endl;
        if (status != 0) {
            ythrow NCachesPrivate::TServerStop(-status);
        }
        status = channel.RecvFromPeer(msgRc);
        if (status != 0) {
            ythrow NCachesPrivate::TServerStop(-status);
        }
        int rcInt = 0;
        if (!::TryFromString<int>(msgRc, rcInt)) {
            ythrow NCachesPrivate::TServerStop(2);
        }
        ythrow NCachesPrivate::TServerStop(rcInt);
    }
    log.ReopenLog();
    channel.CleanupChild();
}

TAutoPtr<NCachesPrivate::IParentChildChannel> NCachesPrivate::Daemonize(TLog& log) {
    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[DAEM]") << "Before starting server" << Endl;

    TAutoPtr<NCachesPrivate::IParentChildChannel> ptr(new NCachesPrivate::TParentChildSocketChannel());
    SetupDaemon(*ptr, log);
#if defined(_linux_)
    // Make sure subreaper closed sockets before communication using ptr starts.
    TAutoPtr<NCachesPrivate::IParentChildChannel> cleanup(new NCachesPrivate::TParentChildSocketChannel());
    // Keep orphaned processes around to be able to kill them later
    if (!NProcUtil::LinuxBecomeSubreaper([&ptr, &cleanup]() -> void { ptr->Cleanup(); cleanup->CleanupParent(); cleanup->SendToPeer("Done"); cleanup->CleanupChild(); })) {
        ythrow(TSystemError(LastSystemError()) << "failed to set subreaper");
    }
    {
        cleanup->CleanupChild();
        TString msg;
        auto status = cleanup->RecvFromPeer(msg);
        if (status != 0) {
            ythrow(TSystemError(LastSystemError()) << "failed to communicate with subreaper: " << status);
        }
        if (msg != "Done") {
            ythrow(TSystemError(LastSystemError()) << "failed to communicate with subreaper, msg: " << msg);
        }
        cleanup->CleanupParent();
    }
#endif
    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[DAEM]") << "Done starting server" << Endl;
    return ptr;
}
