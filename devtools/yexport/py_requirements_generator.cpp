#include "py_requirements_generator.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/lang/makelists/makefile_lang.h>

#include <library/cpp/json/json_reader.h>

#include <util/stream/file.h>

#include <spdlog/spdlog.h>

#include <fmt/format.h>

namespace NYexport {

namespace {

    constexpr std::string_view REQUIRES_FILE = "requirements.txt";

    enum class EModType {
        Yandex,
        Contrib,
        ContribBinding
    };

    const THashMap<TString, TString>& KnownBindingsMap() {
        static const THashMap<TString, TString> bindings{
            {"contrib/libs/some_cpp_binding", "some_cpp_binding"}, // TODO fill with real mapping
        };
        return bindings;
    }

    std::string_view GetModdir(std::string_view graphModPath) {
        return NPath::Parent(NPath::CutAllTypes(graphModPath));
    }

    EModType ModTypeByPath(std::string_view graphPath) {
        const TStringBuf relPath = GetModdir(graphPath);
        if (KnownBindingsMap().contains(relPath)) {
            return EModType::ContribBinding;
        }
        if (relPath.starts_with("contrib/python/") || relPath.starts_with("contrib/deprecated/python/")) {
            return EModType::Contrib;
        }
        return EModType::Yandex;
    }
}

TContribCoords TDefaultContribExtractor::operator()(std::string_view graphModPath) const {
    const auto mk = GetYaMakePath(graphModPath);
    TFileInput in{mk};
    auto ver = ExtractVersion(mk, in);
    if (ver.empty()) {
        const auto mk2 = GetYaMakePath(graphModPath, PyVer_);
        TFileInput in2{mk2};
        ver = ExtractVersion(mk2, in2);
        if (ver.empty()) {
            throw yexception() << fmt::format("{}: No VERSION statement found for contrib", GetModdir(graphModPath));
        }
    }
    return {.Name = GetPipName(graphModPath), .Version = ver};
}

fs::path TDefaultContribExtractor::GetYaMakePath(std::string_view graphModPath) const {
    const std::string_view moddir = GetModdir(graphModPath);
    return ArcadiaRoot_ / moddir / "ya.make";
}

fs::path TDefaultContribExtractor::GetYaMakePath(std::string_view graphModPath, EPyVer pyVer) const {
    const std::string_view moddir = GetModdir(graphModPath);
    std::string_view pyver;
    switch (pyVer) {
        case EPyVer::Py2:
            pyver = "py2";
            break;
        case EPyVer::Py3:
            pyver = "py3";
            break;
    };
    return ArcadiaRoot_ / moddir / pyver / "ya.make";
}

std::string TDefaultContribExtractor::GetPipName(std::string_view graphModPath) const {
    return std::string{NPath::Basename(GetModdir(graphModPath))};
}

std::string TDefaultContribExtractor::ExtractVersion(const fs::path& mkPath, IInputStream& mkContent) const {
    struct: ISimpleMakeListVisitor {
        void Statement(const TStringBuf& command, TVector<TStringBuf>& args, const TVisitorCtx& ctx, const TSourceRange&) final {
            if (command == "VERSION") {
                if (args.size() != 1) {
                    throw yexception() << fmt::format("{}:{}:{} VERSION statement requires single argument", ctx.GetLocation().Path, ctx.GetLocation().Row, ctx.GetLocation().Column);
                }
                Version.assign(args.front());
            }
        }
        void Error(const TStringBuf& message, const TVisitorCtx& ctx) final {
            throw yexception() << fmt::format("{}:{}:{} {}", ctx.GetLocation().Path, ctx.GetLocation().Row, ctx.GetLocation().Column, message);
        }

        std::string Version;
    } versionExtractor;

    auto pool = IMemoryPool::Construct();

    ReadMakeList(mkPath.string(), mkContent.ReadAll(), &versionExtractor, pool.Get());
    return versionExtractor.Version;
}

THolder<TPyRequirementsGenerator> TPyRequirementsGenerator::Load(const fs::path& arcadiaRoot, EPyVer pyVer) {
    return MakeHolder<TPyRequirementsGenerator>(TDefaultContribExtractor{arcadiaRoot, pyVer});
}

void TPyRequirementsGenerator::LoadSemGraph(const std::string&, const fs::path& graph) {
    PyDepsDumpPath_ = graph;
}

/// Get dump of semantics tree with values for testing or debug
void TPyRequirementsGenerator::DumpSems(IOutputStream&) const {
    spdlog::error("Dump semantics of Python generator now yet supported");
}

/// Get dump of attributes tree with values for testing or debug
void TPyRequirementsGenerator::DumpAttrs(IOutputStream&) {
    spdlog::error("Dump attributes of Python generator now yet supported");
}

bool TPyRequirementsGenerator::IgnorePlatforms() const {
    return true;// always ignore platforms
}

void TPyRequirementsGenerator::Render(ECleanIgnored) {
    TFileInput in{PyDepsDumpPath_};
    TFileOutput out = ExportFileManager->Open(REQUIRES_FILE);
    Render(in, out);
}

void TPyRequirementsGenerator::SetProjectName(const std::string&) {
    return;
}

void TPyRequirementsGenerator::Render(IInputStream& pyDepsDump, IOutputStream& dest) const {
    const auto deps = NJson::ReadJsonTree(&pyDepsDump, true);

    THashMap<std::string_view, EModType> modules;
    THashMap<std::string_view, std::string_view> contribPaths;
    for (const auto& elem : deps["data"].GetArray()) {
        if (elem["DataType"] != "Node") {
            continue;
        }

        if (IsModuleType(FromString<EMakeNodeType>(elem["NodeType"].GetString()))) {
            const auto& graphPath = elem["Name"].GetString();
            const auto& id = elem["Id"].GetString();
            const auto modType = ModTypeByPath(graphPath);
            if (modType == EModType::Contrib || modType == EModType::ContribBinding) {
                contribPaths[id] = graphPath;
            }
            modules[elem["Id"].GetString()] = modType;
        }
    }

    THashSet<std::string_view> visited;
    for (const auto& elem : deps["data"].GetArray()) {
        if (elem["DataType"] != "Dep") {
            continue;
        }
        const auto& from = modules.find(elem["FromId"].GetString());
        if (from == modules.end()) {
            continue;
        }
        const auto& toId = elem["ToId"].GetString();
        if (!visited.insert(toId).second) {
            continue;
        }
        const auto& to = modules.find(toId);
        if (to == modules.end()) {
            continue;
        }

        if (from->second == EModType::Yandex && (to->second == EModType::Contrib || to->second == EModType::ContribBinding)) {
            const auto& [name, ver] = ExtractContribCoords_(contribPaths[toId]);
            dest << fmt::format("{}=={}\n", name, ver);
        }
    }
}

}
