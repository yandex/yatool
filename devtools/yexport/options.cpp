#include "options.h"
#include "generators.h"

#include <library/cpp/getopt/small/last_getopt.h>

#include <spdlog/spdlog.h>

namespace {
    const TString OPT_ARCADIA_ROOT = "arcadia-root";
    const TString OPT_EXPORT_ROOT = "export-root";
    const TString OPT_SEM_GRAPH = "semantic-graph";
}

namespace NYexport {

extern fs::path yexportTomlPath(fs::path configDir);
extern std::string GetDefaultGenerator(const fs::path& yexportTomlPath);

TOpts TOpts::Parse(int argc, char** argv) {
    TOpts r;
    NLastGetopt::TOpts opts;
    opts.SetFreeArgsMax(0);

    opts.AddLongOption('E', "events", "Turns on output in evlog format").StoreTrue(&r.LoggingOpts.EnableEvlog);
    opts.AddLongOption('S', "quiet", "Turns off default output in stderr").StoreValue(&r.LoggingOpts.EnableStderr, false);
    opts.AddLongOption('a', OPT_ARCADIA_ROOT, "Path to the arcadia root directory").StoreResult(&r.ArcadiaRoot);
    opts.AddLongOption('L', "evlog-path", "Path where the log file will be created").StoreResult(&r.LoggingOpts.EvLogFilePath);
    opts.AddLongOption('e', OPT_EXPORT_ROOT, "Path to the export output root directory").StoreResult(&r.ExportRoot);
    opts.AddLongOption('j', "project-root", "Path to the project root directory (by default same as export root)").StoreResult(&r.ProjectRoot);
    opts.AddLongOption('c', "configuration", "Path to directory with configuration").StoreResult(&r.ConfigDir);
    opts.AddLongOption('t', "target", "Target project name").StoreResult(&r.ProjectName);
    opts.AddLongOption('s', OPT_SEM_GRAPH, "Path to the semantic graph dump").AppendTo(&r.SemGraphs);
    opts.AddLongOption('p', "platforms", "Platforms to merge: linux, darwin, win").AppendTo(&r.Platforms);
    opts.AddLongOption('C', "clean-ignored", "Remove subdirs with project ignored by the export").StoreTrue(&r.CleanIgnored);
    opts.AddLongOption('I', "report-ignored", "Report subdirs with project ignored by the export as evlog events (noop of events rerting is turned off)").StoreTrue(&r.ReportIgnored);
    opts.AddLongOption('l', "list", "Show a list of available generators").StoreTrue(&r.List);
    opts.AddLongOption('G', "generator", "Generator to use").StoreResult(&r.Generator);
    opts.AddLongOption('P', "py-deps-dump", "Path to the result of `ya dump dep-graph --flat-json-files --no-legacy-deps` result (for python-requirements generator only)")
        .StoreResult(&r.PyDepsDump);
    opts.AddLongOption("py2", "Use python2 contrib version [deprecated]").NoArgument().StoreValue(&r.PyVer, "py2");
    opts.AddLongOption("py3", "Use python3 contrib version [default behavior] [deprecated]").NoArgument().StoreValue(&r.PyVer, "py3");

    opts.AddLongOption(0, "dump-mode", "List divided by '" + DELIMETER + "' of: sems (semantics tree to stdout), attrs (attributes tree to stdout)").Handler1T<std::string>([&](const std::string& arg) {
        auto error = ParseDumpMode(arg, r.DumpOpts);
        if (!error.empty()) {
            spdlog::error(error);
        }
    });
    opts.AddLongOption(0, "dump-path-prefixes", "Dump only this list of relative path prefixes divided by '" + DELIMETER + "'").Handler1T<std::string>([&](const std::string& arg) {
        auto error = ParseDumpPathPrefixes(arg, r.DumpOpts);
        if (!error.empty()) {
            spdlog::error(error);
        }
    });
    opts.AddLongOption(0, "debug-mode", "List divided by '" + DELIMETER + "' of: sems (semantics tree to \"dump_sems\" attribute of each templates), attrs (attributes tree to \"dump_attrs\" attribute of each templates)").Handler1T<std::string>([&](const std::string& arg) {
        auto error = ParseDebugMode(arg, r.DebugOpts);
        if (!error.empty()) {
            spdlog::error(error);
        }
    });
    opts.AddLongOption(0, "fail-on-error", "Generate non-success return code if some errors during exporting").StoreTrue(&r.LoggingOpts.FailOnError);

    NLastGetopt::TOptsParseResult{&opts, argc, argv};

    if (r.PyVer == "py2") {
        r.Generator = NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR;
    }

    if (!(r.Valid = r.Check())) {
        opts.PrintUsage("yexport");
    }

    return r;
}

bool TOpts::Check() {
    bool r = true;
    if (ArcadiaRoot.empty()) {
        ArcadiaRoot = fs::current_path();
        spdlog::info("-a/--{} is not specified. CWD is assumed as Arcadia root: {}", OPT_ARCADIA_ROOT, ArcadiaRoot.string());
        if (List) {
            spdlog::info("Generators list may be incomplete.");
        }
    }
    if (!List) {
        if (ExportRoot.empty()) {
            spdlog::error("--{} required in export mode", OPT_EXPORT_ROOT);
            r = false;
        } else if (ProjectRoot.empty()) {
            ProjectRoot = ExportRoot;
        }
        if (SemGraphs.empty()) {
            spdlog::error("At least one sem-graph at --{} required in export mode", OPT_SEM_GRAPH);
            r = false;
        }
    }
    if (Generator.empty()) {
        if (!ConfigDir.empty()) {
            Generator = GetDefaultGenerator(yexportTomlPath(ConfigDir));
        }
        if (Generator.empty()) {
            Generator = "cmake";
        }
    }
    return r;
}

}
