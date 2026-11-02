#include "ros_processor.h"

#include <devtools/ymake/module_wrapper.h>

namespace {

    using TPackageIncDirs = TMap<TStringBuf, TVector<TStringBuf>>;

    TPackageIncDirs CollectPackages(TIterableCollections<TDirs> incDirs) {
        TPackageIncDirs packages;

        for (const auto& incDir: incDirs) {
            TStringBuf package, path;
            Split(incDir.CutAllTypes(), ':', package, path);
            packages[package].push_back(path);
        }

        return packages;
    }

    TResolveFile ResolveInclude(TModuleWrapper& module, const TPackageIncDirs& packages, const TRosDep& include) {
        auto package = include.PackageName.empty() ? module.UnitName() : include.PackageName;

        auto it = packages.find(package);
        if (it == packages.end()) {
            return {};
        }

        TResolveFile resolveFile;

        for (const auto& path : it->second) {
            const auto searchPath = NPath::Join(path, TString::Join(include.MessageName, ".msg"));

            auto resolveFile = module.ResolveSourcePath(searchPath, {}, TModuleResolver::EResolveFailPolicy::Default, false);

            if (!resolveFile.Empty()) {
                return resolveFile;
            }
        }

        return {};
    }

    TResolveFile MakeInduced(TModuleResolver& module, const TResolveFile& resolvedInclude) {
        TStringBuf resolvedPath = NPath::CutAllTypes(module.GetTargetBuf(resolvedInclude));

        TStringBuf dir = NPath::Parent(resolvedPath);
        TStringBuf name = NPath::BasenameWithoutExtension(resolvedPath);

        TStringBuf dirname = NPath::Basename(dir);
        if (dirname == "msg" || dirname == "srv") {
            dir = NPath::Parent(dir);
        }

        TString inducedPath = NPath::Join(dir, TString::Join(name, ".h"));

        return module.MakeUnresolved(inducedPath);
    }

} // namespace

void TRosIncludeProcessor::ProcessIncludes(
    TAddDepAdaptor& node,
    TModuleWrapper& module,
    TFileView incFileName,
    const TVector<TRosDep>& includes) const
{
    const auto packages = CollectPackages(module.GetModule().IncDirs.Get(LanguageId));

    TVector<TResolveFile> resolvedIncludes;
    TVector<TResolveFile> inducedIncludes;
    const bool splitGeneration = module.Get("_ROS_SPLIT_GENERATION") == "yes";
    if (splitGeneration) {
        // Keep the source name to distinguish e.g. Foo_zero_copy.msg -> Foo_zero_copy.h
        // from Foo.msg -> Foo_zero_copy.h without guessing from the output suffix.
        inducedIncludes.push_back(module.MakeUnresolved(incFileName.CutAllTypes()));
    }

    for (const auto& include : includes) {
        auto resolvedInclude = ResolveInclude(module, packages, include);

        if (resolvedInclude.Empty()) {
            TString typeName = include.PackageName.empty() ? include.MessageName : TString::Join(include.PackageName, "/", include.MessageName);
            continue;
        }

        auto inducedInclude = MakeInduced(module, resolvedInclude);

        resolvedIncludes.push_back(std::move(resolvedInclude));
        inducedIncludes.push_back(std::move(inducedInclude));
    }

    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, module);
    }

    node.AddParsedIncls(splitGeneration ? "ros" : "h+cpp", inducedIncludes);
}

void TRosIncludeProcessor::ProcessOutputIncludes(
    TAddDepAdaptor& node,
    TModuleWrapper& module,
    TFileView incFileName,
    const TVector<TString>& includes) const
{
    Y_UNUSED(node);
    Y_UNUSED(module);
    Y_UNUSED(incFileName);
    Y_UNUSED(includes);

    YConfErr(UserErr) << "OUTPUT_INCLUDES are not supported for ROS" << Endl;
}

TVector<TString> MapRosOutputIncludes(TFileView output, const TVector<TStringBuf>& props) {
    if (props.empty()) {
        return {};
    }
    TStringBuf suffix = output.Basename();
    if (!suffix.SkipPrefix(NPath::BasenameWithoutExtension(props.front()))) {
        return {};
    }
    if (NPath::Extension(props.front()) == "srv") {
        // The service wrapper includes its request/response headers explicitly.
        if (!suffix.SkipPrefix("Request") && !suffix.SkipPrefix("Response")) {
            return {};
        }
    }
    if (suffix != ".h" && suffix != "_zero_copy.h" && suffix != "_ros_converter.h") {
        // Sources include their own header; binding declarations need none.
        return {};
    }
    TVector<TString> includes;
    includes.reserve(props.size() - 1);
    for (size_t i = 1; i < props.size(); ++i) {
        includes.push_back(TString::Join(NPath::NoExtension(props[i]), suffix));
    }
    return includes;
}
