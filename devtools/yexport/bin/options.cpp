#include "options.h"

#include "../generators.h"
#include "../py_requirements_generator.h"

#include <library/cpp/getopt/small/last_getopt.h>

using namespace NYexport;

TOpts TOpts::Parse(int argc, char** argv) {
    TOpts ret;
    NLastGetopt::TOpts opts;
    opts.SetFreeArgsMax(0);

    opts.AddLongOption('E', "events", "Turns on output in evlog format").StoreTrue(&ret.LoggingOpts.EnableEvlog);
    opts.AddLongOption('S', "quiet", "Turns off default output in stderr").StoreValue(&ret.LoggingOpts.EnableStderr, false);
    opts.AddLongOption('a', "arcadia-root", "Path to the arcadia root directory").StoreResult(&ret.ArcadiaRoot);
    opts.AddLongOption('L', "evlog-path", "Path where the log file will be created").StoreResult(&ret.LoggingOpts.EvLogFilePath);
    opts.AddLongOption('e', "export-root", "Path to the export output root directory").StoreResult(&ret.ExportRoot);
    opts.AddLongOption('c', "configuration", "Path to directory with configuration").StoreResult(&ret.ConfigDir);
    opts.AddLongOption('t', "target", "Target project name").StoreResult(&ret.ProjectName);
    opts.AddLongOption('s', "semantic-graph", "Path to the semantic graph dump").AppendTo(&ret.SemGraphs);
    opts.AddLongOption('p', "platforms", "Platforms to merge: linux, darwin, win").AppendTo(&ret.Platforms);
    opts.AddLongOption('C', "clean-ignored", "Remove subdirs with project ignored by the export").StoreTrue(&ret.CleanIgnored);
    opts.AddLongOption('l', "list", "Show a list of available generators").StoreTrue(&ret.List);
    opts.AddLongOption('G', "generator", "Generator to use")
        .StoreResult(&ret.Generator)
        .DefaultValue(NGenerators::HARDCODED_CMAKE_GENERATOR);
    opts.AddLongOption('P', "py-deps-dump", "Path to the result of `ya dump dep-graph --flat-json-files --no-legacy-deps` result (for python-requirements generator only)")
        .StoreResult(&ret.PyDepsDump);
    opts.AddLongOption("py2", "Use python2 contrib version [deprecated]").NoArgument().StoreValue(&ret.PyVer, "py2");
    opts.AddLongOption("py3", "Use python3 contrib version [default behavior] [deprecated]").NoArgument().StoreValue(&ret.PyVer, "py3");
    NLastGetopt::TOptsParseResult{&opts, argc, argv};

    if (ret.PyVer == "py2") {
        ret.Generator = NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR;
    }

    return ret;
}
