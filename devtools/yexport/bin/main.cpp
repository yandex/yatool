#include "options.h"
#include "logging.h"

#include "../generators.h"
#include "../generator_spec.h"
#include "../read_sem_graph.h"
#include "../yexport_generator.h"

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/system/file.h>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <filesystem>

int main(int argc, char** argv) try {
    auto opts = TOpts::Parse(argc, argv);

    SetupLogger(opts.LoggingOpts);

    if (opts.List) {
        if (opts.ArcadiaRoot.empty()) {
            opts.ArcadiaRoot = fs::current_path();
            spdlog::info("-a/--arcadia-root is not scpecified. CWD is assumed as Arcadia root: {}", opts.ArcadiaRoot.string());
            spdlog::info("Generators list may be incomplete.");
        }
        for (auto const& generator : GetAvailableGenerators(opts.ArcadiaRoot)) {
            fmt::print("  {}\n", generator);
        }
        return 0;
    }

    auto generator = Load(opts.Generator, opts.ArcadiaRoot, opts.ConfigDir);
    generator->SetProjectName(opts.ProjectName);

    if (opts.Generator == NGenerators::HARDCODED_CMAKE_GENERATOR) {
        if (opts.Platforms.size() != opts.SemGraphs.size()) {
            spdlog::error("Number of platforms isn't equal to number of semantic graphs.");
            return 1;
        }
        for (size_t i = 0; i < opts.SemGraphs.size(); i++) {
            generator->LoadSemGraph(opts.Platforms[i], opts.SemGraphs[i]);
        }
    } else if (opts.Generator == NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR || opts.Generator == NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR) {
        if (opts.PyDepsDump.empty()) {
            spdlog::error("path to py dependency dump is required for the {} generator", opts.Generator);
            return 1;
        }
        generator->LoadSemGraph("", opts.PyDepsDump);
    } else {
        if (opts.SemGraphs.size() != 1) {
            spdlog::error("Requires exactly one semantic graph while using generator");
            return 1;
        }
        generator->LoadSemGraph("", opts.SemGraphs.front());
    }
    ECleanIgnored cleanIgnored = opts.CleanIgnored ? ECleanIgnored::Enabled : ECleanIgnored::Disabled;
    generator->RenderTo(opts.ExportRoot, cleanIgnored);

    return 0;
} catch (const TReadGraphException& err) {
    spdlog::error("{}", err.what());
    return 1;
} catch (const TBadGeneratorSpec& err) {
    spdlog::error("{}", err.what());
    return 1;
} catch (const std::system_error& err) {
    spdlog::error("{}", err.what());
    return 1;
}
