#include "runner.h"

#include <util/system/shellcommand.h>
#include <util/thread/factory.h>
#include <util/system/event.h>

namespace NUniversalFetcher {

    namespace {

        class TProcessRunner: public IProcessRunner {
        public:
            TResult Run(const TVector<TString>& argv, TDuration timeout) override {
                const auto deadline = timeout.ToDeadLine();
                TShellCommandOptions opts;
                opts.SetCloseAllFdsOnExec(true);
                opts.SetUseShell(false);
                if (timeout) {
                    opts.SetAsync(true);
                }
                auto program = argv[0];
                TShellCommand cmd(program, {argv.begin() + 1, argv.end()}, opts);
                if (timeout) {
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
                    .StdErr = cmd.GetError(),
                };
            }
        };

    }

    TProcessRunnerPtr CreateDirectProcessRunner() {
        return new TProcessRunner();
    }

}
