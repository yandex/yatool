#include "trace_start.h"

#include <util/generic/fwd.h>
#include <util/system/env.h>
#include <util/system/shellcommand.h>

void TraceYmakeStart(int argc, char** argv) {
    TString traceProgram = GetEnv("YMAKE_START_TRACE_PROGRAM");

    if (traceProgram.Empty())
        return;

    TShellCommandOptions opts;
    opts.SetUseShell(false);
    opts.SetAsync(false);

    TList<TString> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);

    TShellCommand cmd{traceProgram, args, opts};
    cmd.Run();
}
