#pragma once

#include <devtools/yexport/std_helpers.h>
#include <devtools/yexport/dump.h>
#include <devtools/yexport/debug.h>

#include <util/generic/vector.h>
#include <filesystem>

namespace NYexport {

struct TLoggingOpts {
    bool EnableEvlog = false;
    bool EnableStderr = true;
    fs::path EvLogFilePath;
};

struct TOpts {
    fs::path ArcadiaRoot;
    fs::path ExportRoot;
    fs::path ConfigDir;
    std::string ProjectName;
    TVector<fs::path> SemGraphs;
    TVector<std::string> Platforms;
    std::string Generator;

    bool CleanIgnored = false;
    bool ReportIgnored = false;
    TDumpOpts DumpOpts;
    TDebugOpts DebugOpts;

    TLoggingOpts LoggingOpts;

    fs::path PyDepsDump;
    std::string PyVer = "py3";

    /// Output a list of generators.
    bool List = false;

    static TOpts Parse(int argc, char** argv);
};

}
