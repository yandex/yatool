#pragma once

#include <util/generic/vector.h>
#include <filesystem>

namespace fs = std::filesystem;

struct TGeneratorArgs;

struct TLoggingOpts {
    bool EnableEvlog = false;
    bool EnableStderr = true;
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

    TLoggingOpts LoggingOpts;

    fs::path PyDepsDump;
    std::string PyVer = "py3";

    /// Output a list of generators.
    bool List = false;

    static TOpts Parse(int argc, char** argv);
};
