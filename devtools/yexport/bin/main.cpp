#include <devtools/yexport/generators.h>
#include <devtools/yexport/logging.h>
#include <devtools/yexport/options.h>
#include <devtools/yexport/spec_based_generator.h>
#include <devtools/yexport/yexport_generator.h>

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/system/file.h>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <filesystem>

using namespace NYexport;

int main(int argc, char** argv) try {
    auto opts = TOpts::Parse(argc, argv);
    if (!opts.Valid) {
        return 1;
    }

    SetupLogger(opts.LoggingOpts);

    if (opts.List) {
        for (auto const& generator : GetAvailableGenerators(opts.ArcadiaRoot)) {
            fmt::print("  {}\n", generator);
        }
        return 0;
    }

    auto generator = Load(opts);
    generator->SetProjectName(opts.ProjectName);

    if (opts.Generator == NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR || opts.Generator == NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR) {
        if (opts.PyDepsDump.empty()) {
            spdlog::error("path to py dependency dump is required for the {} generator", opts.Generator);
            return 1;
        }
        generator->LoadSemGraph("", opts.PyDepsDump);
    }
    if (opts.Platforms.empty() || generator->IgnorePlatforms()) { // no platforms, load strong one semgraph with empty platform name
        if (opts.SemGraphs.size() != 1) {
            spdlog::error("Requires exactly one semantic graph while using generator {}", opts.Generator);
            return 1;
        }
        generator->LoadSemGraph("", opts.SemGraphs.front());
    } else { // count of platforms must be equal count of semgraph
        if (opts.Platforms.size() != opts.SemGraphs.size()) {
            spdlog::error("Number of platforms isn't equal to number of semantic graphs.");
            return 1;
        }
        for (size_t i = 0; i < opts.SemGraphs.size(); i++) {
            generator->LoadSemGraph(opts.Platforms[i], opts.SemGraphs[i]);
        }
    }
    if (opts.DumpOpts.DumpSems || opts.DumpOpts.DumpAttrs) {
        if (opts.DumpOpts.DumpSems) {
            generator->DumpSems(Cout);
        }
        if (opts.DumpOpts.DumpAttrs) {
            generator->DumpAttrs(Cout);
        }
        return 0;
    }

    ECleanIgnored cleanIgnored = opts.CleanIgnored ? ECleanIgnored::Enabled : ECleanIgnored::Disabled;
    generator->RenderTo(opts.ExportRoot, opts.ProjectRoot, cleanIgnored);

    if (opts.LoggingOpts.FailOnError && IsFailOnError()) {
        spdlog::error("There were errors during export, generate an non-success return code");
        return 1;
    }
    return 0;
} catch (const TYExportException& err) {
    spdlog::error("Caught TYExportException: {}", err.what());
    auto trace = err.GetCallStack();
    for (size_t i = 0; i < trace.size(); ++i) {
        spdlog::error("#{}: {}", i, trace[i]);
    }
    return 1;
} catch (const std::exception& err) {
    spdlog::error("Caught exception: {}", err.what());
    return 1;
}
