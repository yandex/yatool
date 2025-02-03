#include "runner.h"

#include <util/system/shellcommand.h>
#include <util/thread/factory.h>
#include <util/system/event.h>

namespace NUniversalFetcher {

    namespace {

        class TProcessRunner: public IProcessRunner {
        public:
            TResult Run(const TVector<TString>& argv, const TRunParams& params, TCancellationToken cancellation = TCancellationToken::Default()) override {
                const auto deadline = params.Timeout.ToDeadLine();
                TShellCommandOptions opts;
                opts.SetCloseAllFdsOnExec(true);
                opts.SetUseShell(false);
                if (params.Timeout) {
                    opts.SetAsync(true);
                }
                opts.SetOutputStream(params.OutputStream);
                opts.SetErrorStream(params.ErrorStream);
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
