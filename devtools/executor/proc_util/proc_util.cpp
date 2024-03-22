#include "proc_util.h"

#if defined(__linux__)
    #include <errno.h>
    #include <sys/prctl.h>
    #include <sys/wait.h>
    #include <sched.h>
    #include <unistd.h>

    #include <util/stream/file.h>
    #include <util/folder/path.h>
    #include <util/string/builder.h>

    #ifndef PR_SET_CHILD_SUBREAPER
        #define PR_SET_CHILD_SUBREAPER 36
    #endif
#endif

#include <devtools/executor/proc_info/proc_info.h>
#include <devtools/executor/net/netns.h>

#include <library/cpp/deprecated/atomic/atomic.h>

#include <util/generic/adaptor.h>
#include <util/generic/yexception.h>
#include <util/system/getpid.h>
#include <util/system/sigset.h>
#include <util/system/winint.h>

namespace {
#if defined(_linux_)
    pid_t SubreaperChildPid;
    TAtomic SubreaperChildStatus(-1);

    void ForwardSignal(int sig) {
        int oerrno = errno;
        kill(SubreaperChildPid, sig);
        errno = oerrno;
    }

    int ShellExitCode(int status) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? WTERMSIG(status) + 128 : 0);
    }

    void SigChildHandler(int) {
        int oerrno = errno;
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if (pid == SubreaperChildPid) {
                AtomicCas(&SubreaperChildStatus, ShellExitCode(status), -1);
            }
        }
        errno = oerrno;
    }
#endif

    void TerminateProc(pid_t pid) {
#if defined(_win_)
        auto handle = OpenProcess(PROCESS_TERMINATE, false, pid);
        // Process might be already terminated
        if (handle) {
            try {
                TerminateProcess(handle, 1);
            } catch (...) {
                Cerr << "Failed to terminate process: " << LastSystemErrorText() << Endl;
            }
            CloseHandle(handle);
        }
#else
        kill(pid, SIGKILL);
#endif
    }

    void StopProc(pid_t pid) {
#if defined(_unix_)
        kill(pid, SIGSTOP);
#endif
    }
}
#ifndef CLONE_NEWUTS
    #define CLONE_NEWUTS 0x04000000
#endif
#ifndef CLONE_NEWNET
    #define CLONE_NEWNET 0x40000000
#endif
#ifndef CLONE_NEWUSER
    #define CLONE_NEWUSER 0x10000000
#endif

namespace NProcUtil {
    void TSubreaperApplicant::Close() {
#if defined(_win_)
        // All associated processes with job will be terminated
        if (JobHandle) {
            CloseHandle(JobHandle);
        }
#endif
    }

    TSubreaperApplicant::TSubreaperApplicant() {
#if defined(_linux_)
        // Keep orphaned processes around to be able to kill them later
        if (!NProcUtil::LinuxBecomeSubreaper()) {
            Cerr << "Failed to set subreaper: " << LastSystemErrorText() << Endl;
        }
#endif
#if defined(_win_)
        JobHandle = NProcUtil::WinCreateSubreaperJob();
        if (!JobHandle) {
            Cerr << "Failed to create subreaper job: " << LastSystemErrorText() << Endl;
        }
#endif
    }

    void TerminateChildren() {
        TVector<pid_t> prev;
        TVector<pid_t> children;
        auto selfPid = GetPID();
        do {
            prev = children;
            children = NProcInfo::GetChildrenPids(selfPid, true);
            for (auto pid : children) {
                StopProc(pid);
            }
            for (auto pid : Reversed(children)) {
                TerminateProc(pid);
            }
            // There might be nonkillable processes (zombies, processes that are in the core dumping state, etc)
        } while (!children.empty() && children != prev);
    }

#if defined(_linux_)
    void SetGroupsDeny() {
        const TFsPath filename = TFsPath("/proc/self/setgroups");
        TFileOutput{filename}.Write("deny");
    }

    void MapId(TString& filename, uint32_t from, uint32_t to) {
        const TFsPath path = TFsPath(filename);
        TFileOutput{path}.Write(TStringBuilder() << from << ' ' << to << ' ' << 1);
    }

    void UnshareNs() {
        char hostbuffer[256];
        gethostname(hostbuffer, sizeof(hostbuffer));
        TString newHostName(hostbuffer);

        uid_t real_euid = geteuid();
        gid_t real_egid = getegid();
        int unshare_flags = CLONE_NEWNET | CLONE_NEWUSER | CLONE_NEWUTS;

        unshare(unshare_flags);
        sethostname(newHostName.c_str(), static_cast<int>(newHostName.size()));

        TString umap = "/proc/self/uid_map";
        TString gmap = "/proc/self/gid_map";

        MapId(umap, 0, real_euid);
        SetGroupsDeny();
        MapId(gmap, 0, real_egid);

        // setting localhost up
        NNetNs::IfUp("lo", "10.1.1.1", "255.255.255.0");
    }

    bool LinuxBecomeSubreaper(std::function<void()> cleanupAfterFork) {
        if (SubreaperChildPid) {
            return true;
        }
        if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) {
            return false;
        }
        sigset_t oldmask, newmask;
        Y_ABORT_UNLESS(SigFillSet(&newmask) == 0);
        Y_ABORT_UNLESS(SigProcMask(SIG_SETMASK, &newmask, &oldmask) == 0);

        SubreaperChildPid = fork();
        if (SubreaperChildPid < 0) {
            ythrow TSystemError() << "Cannot fork";
        } else if (SubreaperChildPid == 0) {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            Y_ABORT_UNLESS(SigProcMask(SIG_SETMASK, &oldmask, nullptr) == 0);
            return true;
        }
        // Parent
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;

        sa.sa_handler = SigChildHandler;
        Y_ABORT_UNLESS(sigaction(SIGCHLD, &sa, nullptr) == 0);

        sa.sa_handler = ForwardSignal;
        Y_ABORT_UNLESS(sigaction(SIGABRT, &sa, nullptr) == 0);
        Y_ABORT_UNLESS(sigaction(SIGHUP, &sa, nullptr) == 0);
        Y_ABORT_UNLESS(sigaction(SIGINT, &sa, nullptr) == 0);
        Y_ABORT_UNLESS(sigaction(SIGQUIT, &sa, nullptr) == 0);
        Y_ABORT_UNLESS(sigaction(SIGTERM, &sa, nullptr) == 0);
        Y_ABORT_UNLESS(SigProcMask(SIG_SETMASK, &oldmask, nullptr) == 0);

        cleanupAfterFork();

        int status = 0;
        pid_t pid = 0;
        while (SubreaperChildStatus == -1 && (pid = waitpid(SubreaperChildPid, &status, 0)) < 0 && errno == EINTR) {
        }
        if (pid == SubreaperChildPid) {
            AtomicCas(&SubreaperChildStatus, ShellExitCode(status), -1);
        }
        TerminateChildren();
        _exit(SubreaperChildStatus);
        return true;
    }
#elif defined(_win_)
    void* WinCreateSubreaperJob() {
        HANDLE jobHandle = CreateJobObject(nullptr, nullptr);
        if (!jobHandle) {
            return nullptr;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
            return nullptr;
        }
        if (!AssignProcessToJobObject(jobHandle, GetCurrentProcess())) {
            return nullptr;
        }
        return jobHandle;
    }
#endif
}
