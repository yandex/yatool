#include "runner.h"

#include <util/system/shellcommand.h>
#include <util/thread/factory.h>
#include <util/system/event.h>

#if defined(_linux_)
    #include <fcntl.h>
    #include <syscall.h>
    #include <unistd.h>
#elif defined(_darwin_)
    #include <sys/types.h>
    #include <dirent.h>
    #include <unistd.h>
#endif

#if defined(_msan_enabled_)
    #include <util/system/sanitizers.h>
#endif

namespace NUniversalFetcher {

    namespace {

#if defined(_linux_) || defined(_darwin_)
        int StrToInt(const char* name) {
            int val = 0;
            for (; *name; ++name) {
                if ((*name) < '0' || (*name) > '9') {
                    return -1;  // Not a number
                }
                val = val * 10 + (*name - '0');
            }
            return val;
        }
#endif

        /*
            Copy-pasted from python3 subprocess (contrib/tools/python3/Modules/_posixsubprocess.c)
        */

        void CloseHandles() {

#if defined(_linux_)
            /*
                opendir/readdir/closedir allocate memory so are unsafe after fork (may cause deadlocks in libc).
                So on Linux we have to use the getdents64 syscall to read directory. Sad but true.
                Closing all possible handles between 3 and max open files (getdtablesize()) can be VERY slow:
                for max open files = 1000000 it takes about 300 ms.
            */
            // https://man7.org/linux/man-pages/man2/getdents.2.html
            struct linux_dirent64 {
                ino64_t        d_ino;    /* 64-bit inode number */
                off64_t        d_off;    /* 64-bit offset to next structure */
                unsigned short d_reclen; /* Size of this dirent */
                unsigned char  d_type;   /* File type */
                char           d_name[]; /* Filename (null-terminated) */
            };

            char buffer[sizeof(linux_dirent64) * 8];
            int fd_dir = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
            if (fd_dir == -1) {
                return;
            }
            int nread;
            while ((nread = syscall(SYS_getdents64, fd_dir, (struct linux_dirent64 *)buffer, sizeof(buffer))) > 0) {
#if defined(_msan_enabled_)
                __msan_unpoison(buffer, nread);
#endif
                for (int bpos = 0; bpos < nread; ) {
                    const linux_dirent64* dirent = (struct linux_dirent64 *) (buffer + bpos);
                    int fd = StrToInt(dirent->d_name);
                    if (fd != fd_dir && fd > STDERR_FILENO) {
                        close(fd);
                    }
                    bpos += dirent->d_reclen;
                }
            }
            close(fd_dir);

#elif  defined(_darwin_)
            if (DIR* fd_dir = opendir("/dev/fd")) {
                int fd_used_by_opendir = dirfd(fd_dir);
                struct dirent* dir_entry;
                while (dir_entry = readdir(fd_dir)) {
                    if (int fd = StrToInt(dir_entry->d_name); fd != fd_used_by_opendir && fd > STDERR_FILENO) {
                        close(fd);
                    }
                }
                closedir(fd_dir);
            }
#endif
        }

        class TProcessRunner: public IProcessRunner {
        public:
            TResult Run(const TVector<TString>& argv, const TRunParams& params, TCancellationToken cancellation = TCancellationToken::Default()) override {
                const auto deadline = params.Timeout.ToDeadLine();
                TShellCommandOptions opts;
                opts.SetUseShell(false);
                if (params.Timeout) {
                    opts.SetAsync(true);
                }
                opts.SetOutputStream(params.OutputStream);
                opts.SetErrorStream(params.ErrorStream);
                opts.SetFuncAfterFork(CloseHandles);
                auto program = argv[0];
                TShellCommand cmd(program, {argv.begin() + 1, argv.end()}, opts);

                // Cancellation
                cancellation.Future().Subscribe([&cmd](const auto&){
                    cmd.Terminate();
                });

                if (params.Timeout) {
                    cmd.Run();
                    TManualEvent stopped;
                    auto t = SystemThreadFactory()->Run([&]() {
                        cmd.Wait();
                        stopped.Signal();
                    });
                    stopped.WaitD(deadline);
                    cmd.Terminate();
                } else {
                    cmd.Run();
                }
                return {
                    .ExitStatus = *cmd.GetExitCode(),
                    .StdOut = cmd.GetOutput(),
                    .StdErr = cmd.GetError(),
                };
            }
        };

    }

    TProcessRunnerPtr CreateDirectProcessRunner() {
        return new TProcessRunner();
    }

}
