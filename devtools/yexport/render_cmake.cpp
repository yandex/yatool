#include "render_cmake.h"
#include "cmake_generator.h"
#include "std_helpers.h"
#include "project.h"
#include "graph_visitor.h"
#include "internal_attributes.h"

#include <devtools/ymake/compact_graph/query.h>

#include <library/cpp/resource/resource.h>

#include <util/generic/set.h>
#include <util/generic/map.h>
#include <util/string/type.h>
#include <util/system/fs.h>

#include <spdlog/spdlog.h>

#include <span>
#include <type_traits>

namespace NYexport {

using namespace std::literals;

namespace {
    constexpr std::string_view YMAKE_SOURCE_PREFIX = "$S/";
    constexpr std::string_view YMAKE_BUILD_PREFIX = "$B/";

    enum class ECMakeAttrScope {Private, Public, Interface};
    enum class EMacroMergePolicy {MultipleCalls, FirstCall, ConcatArgs};

    ECMakeAttrScope ParseCMakeScope(TStringBuf val) {
        if (val == "PUBLIC") {
            return ECMakeAttrScope::Public;
        } else if (val == "PRIVATE") {
            return ECMakeAttrScope::Private;
        } else if (val == "INTERFACE") {
            return ECMakeAttrScope::Interface;
        }
        throw yexception() << "Unknown cmake scope string '" << val << "' (PRIVATE, PUBLIC or INTERFACE required)";
    }

    void SplitToolPath(TStringBuf toolRelPath, TStringBuf& arcadiaPath, TStringBuf& toolName) {
        toolRelPath.RSplit('/', arcadiaPath, toolName);
        toolName.ChopSuffix(".exe"sv);
        if (arcadiaPath.empty() || toolName.empty()) {
            spdlog::error("bad tool path '{}', cannot be split to arcadiaPath and toolName", toolRelPath);
        }
    }

    std::string GetToolBinVariable(TStringBuf toolName) {
        return fmt::format("TOOL_{}_bin", toolName);
    }

    std::string QuoteAndEscapeValues(const std::span<const std::string> values, char divider = ';') {
        size_t valuesSize = 0;
        for (const auto& value: values) {
            valuesSize += value.size();
        }
        std::string escaped;
        escaped.reserve(2/*quotes*/ + values.size() - 1/*dividers*/ + valuesSize);
        escaped += '"';
        for (const auto& value: values) {
            for (const auto c: value) {
                if (c == '"' || c == '\\' || c == divider) {
                    escaped += '\\';
                }
                escaped += c;
            }
            if (value != values.back()) {
                escaped += divider;
            }
        }
        escaped += '"';
        return escaped;
    }

    struct TAttrValues {
        TVector<std::string> Iface;
        TVector<std::string> Pub;
        TVector<std::string> Priv;
    };

    struct TCMakeTarget : TProjectTarget {
        TMap<std::string, TAttrValues> Attributes;
        TMap<std::string, TVector<std::string>> Properties;
        TMultiMap<std::string, TVector<std::string>> Macros;
        TVector<std::pair<std::string, TVector<std::string>>> DirMacros;

        // target names for tools, used if cross-compilation is not enabled
        TSet<std::string> HostToolExecutableTargetDependencies;
        bool InterfaceTarget = false;
    };

    using TInducedCmakePackages = TMap<std::string, TSet<std::string>>;

    struct TCMakeList : TProjectSubdir {
        TInducedCmakePackages Packages;
        TVector<std::string> Includes;
        TVector<std::pair<std::string, TVector<std::string>>> Macros;
        TSet<fs::path> SubdirectoriesToAdd;
    };

    using TSubdirsTable = THashMap<fs::path, TCMakeList>;
    using TSubdirsTableElem = THashMap<fs::path, TCMakeList>::value_type;

    class TCMakeProject : public TProject {
    public:
        class TBuilder;

        TCMakeProject()
            : TProject()
        {
            SetFactoryTypes<TCMakeList, TCMakeTarget>();
        }

        THashMap<fs::path, TSet<fs::path>> GetSubdirsTable() const {
            THashMap<fs::path, TSet<fs::path>> result;
            for (auto subdir: Subdirs_) {
                result.insert({subdir->Path, subdir.As<TCMakeList>()->SubdirectoriesToAdd});
            }
            return result;
        }

    private:

        template<typename T>
        static jinja2::ValuesList ConvertParameterizedMacros(
                const T& macros
        ) {
            auto result = jinja2::ValuesList();
            result.reserve(macros.size());
            for (const auto& [macro, values]: macros) {
                result.emplace_back(jinja2::ValuesList{macro, jinja2::ValuesList(values.begin(), values.end())});
            }
            return result;
        }

        static TAttrsPtr MakeTargetAttrs(
                const TCMakeTarget* tgt,
                const TCMakeGenerator* cmakeGenerator
        ) {
            auto name = "target " + tgt->Macro + " " + tgt->Name;
            TAttrsPtr targetAttrs = cmakeGenerator->MakeAttrs(EAttrGroup::Target, name);
            auto& targetMap = targetAttrs->GetWritableMap();

            // Target definition
            targetMap.insert_or_assign("macro", tgt->Macro.c_str());
            targetMap.insert_or_assign("name", tgt->Name.c_str());
            targetAttrs->SetAttrValue("is_interface", tgt->InterfaceTarget ? jinja2::ValuesList{"true"} : jinja2::ValuesList{"false"}, name);

            {
                const auto [_, inserted] = NInternalAttrs::EmplaceAttr(targetMap, NInternalAttrs::MacroArgs, jinja2::ValuesList(tgt->MacroArgs.begin(), tgt->MacroArgs.end()));
                Y_ASSERT(inserted);
            }

            // Target properties
            {
                auto [propertiesIt, inserted] = targetMap.emplace("properties", jinja2::ValuesList());
                Y_ASSERT(inserted);
                auto& properties = propertiesIt->second.asList();
                for (const auto& [name, values]: tgt->Properties) {
                    if (!values.empty()) {
                        properties.emplace_back(jinja2::ValuesList{name, jinja2::ValuesList(values.begin(), values.end())});
                    }
                }
            }

            // Target attributes
            {
                auto [attributesIt, inserted] = targetMap.emplace("attributes", jinja2::ValuesList());
                Y_ASSERT(inserted);
                auto& attributes = attributesIt->second.asList();
                for (const auto& [name, values]: tgt->Attributes) {
                    jinja2::ValuesMap valuesMap;
                    if (!values.Iface.empty()) {
                        const auto [_, inserted] = valuesMap.emplace("iface", jinja2::ValuesList(values.Iface.begin(), values.Iface.end()));
                        Y_ASSERT(inserted);
                    }
                    if (!values.Pub.empty()) {
                        const auto [_, inserted] = valuesMap.emplace("pub", jinja2::ValuesList(values.Pub.begin(), values.Pub.end()));
                        Y_ASSERT(inserted);
                    }
                    if (!values.Priv.empty()) {
                        const auto [_, inserted] = valuesMap.emplace("priv", jinja2::ValuesList(values.Priv.begin(), values.Priv.end()));
                        Y_ASSERT(inserted);
                    }
                    attributes.emplace_back(jinja2::ValuesList{name, valuesMap});
                }
            }

            // Target-related dir level macros
            {
                const auto [_, inserted] = targetMap.emplace("target_dir_macros", ConvertParameterizedMacros(tgt->DirMacros));
                Y_ASSERT(inserted);
            }

            // Target level macros
            {
                const auto [_, inserted] = targetMap.emplace("target_macros", ConvertParameterizedMacros(tgt->Macros));
                Y_ASSERT(inserted);
            }

            // Dependencies
            {
                jinja2::ValuesList dependencies(tgt->HostToolExecutableTargetDependencies.begin(), tgt->HostToolExecutableTargetDependencies.end());
                const auto [_, inserted] = targetMap.emplace("dependencies", jinja2::ValuesList(tgt->HostToolExecutableTargetDependencies.begin(), tgt->HostToolExecutableTargetDependencies.end()));
                Y_ASSERT(inserted);
            }

            return targetAttrs;
        }

    public:
        TAttrsPtr GetSubdirValuesMap(const TCMakeList& data, const TCMakeGenerator* cmakeGenerator) const {
            auto name = "dir " + data.Path.string();
            TAttrsPtr dirAttrs = cmakeGenerator->MakeAttrs(EAttrGroup::Directory, name);
            auto& dirMap = dirAttrs->GetWritableMap();

            {
                auto [packagesIt, inserted] = dirMap.emplace("packages", jinja2::ValuesList());
                Y_ASSERT(inserted);
                auto& packages = packagesIt->second.asList();
                for (const auto& [name, components]: data.Packages) {
                    packages.emplace_back(jinja2::ValuesList{name, jinja2::ValuesList(components.begin(), components.end())});
                }
            }

            {
                THashSet<std::string_view> dedup;
                for (const auto& include: data.Includes) {
                    dedup.insert(include);
                }

                const auto [_, inserted] = dirMap.emplace("includes", jinja2::ValuesList(dedup.begin(), dedup.end()));
                Y_ASSERT(inserted);
            }

            // add subdirectories that contain targets inside (possibly several levels lower)
            {
                auto [subdirsIt, inserted] = NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::Subdirs, jinja2::ValuesList());
                Y_ASSERT(inserted);
                auto& subdirs = subdirsIt->second.asList();
                for (const auto& dir: data.SubdirectoriesToAdd) {
                    subdirs.emplace_back(dir.string());
                }
            }

            const auto [_, inserted] = dirMap.emplace("dir_macros", ConvertParameterizedMacros(data.Macros));
            Y_ASSERT(inserted);

            {
                auto [extraTargetsIt, inserted] = NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::ExtraTargets, jinja2::ValuesList());
                Y_ASSERT(inserted);
                auto& extraTargets = extraTargetsIt->second.asList();
                for (auto tgt: data.Targets) {
                    const auto cmakeTarget = tgt.As<TCMakeTarget>().Get();
                    auto targetAttrs = MakeTargetAttrs(cmakeTarget, cmakeGenerator)->GetMap();
                    if (cmakeTarget->Macro == "add_global_library_for" ||
                        cmakeTarget->Macro == "add_recursive_library" ||
                        cmakeTarget->Macro == "add_swig_jni_library") {
                        extraTargets.emplace_back(targetAttrs);
                    } else {
                        NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::Target, targetAttrs);
                    }
                }
            }
            return dirAttrs;
        }
    };
    using TCMakeProjectPtr = TSimpleSharedPtr<TCMakeProject>;

    class TCMakeProject::TBuilder: public TProject::TBuilder {
    public:

        TBuilder(const TPlatformPtr platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
        : TProject::TBuilder(cmakeGenerator)
        , Platform(platform)
        , GlobalProperties(globalProperties)
        , CMakeGenerator(cmakeGenerator)
        {
            Project_ = MakeSimpleShared<TCMakeProject>();
        }

        void CustomFinalize() override {
            for (auto subdir : Project_->GetSubdirs()) {
                auto& cmakeList = *subdir.As<TCMakeList>();
                for (const auto& child : cmakeList.Subdirs) {
                    cmakeList.SubdirectoriesToAdd.insert(child->Path.filename());
                }
            }
        }

        void CustomOnAttribute(const std::string& attrName, const std::span<const std::string>& /*attrValue*/) override {
            for (const auto& rulePtr : CMakeGenerator->GetGeneratorSpec().GetAttrRules(attrName)) {
                const auto& addValues = rulePtr->AddValues;
                if (auto valuesIt = addValues.find("includes"); valuesIt != addValues.end()) {
                    for (const auto& value : valuesIt->second) {
                        GlobalProperties.GlobalModules.emplace(value);
                    }
                }
            }
        }

        void SetCurrentTools(TMap<std::string_view, std::string_view> tools) noexcept {
            Tools = std::move(tools);
        }

        void AddTargetAttr(const std::string& attrMacro, ECMakeAttrScope scope, std::span<const std::string> vals) {
            if (!CurTarget_) {
                spdlog::info("attempt to add target attribute '{}' while there is no active target", attrMacro);
                return;
            }
            auto& curCmakeTarget = *CurTarget_.As<TCMakeTarget>();
            if (vals.empty()) {
                return;
            }
            if (curCmakeTarget.InterfaceTarget && attrMacro == "target_sources") {
                scope = ECMakeAttrScope::Interface;
            }

            TVector<std::string>* dest;
            switch(scope) {
            case ECMakeAttrScope::Interface:
                dest = &curCmakeTarget.Attributes[attrMacro].Iface;
            break;
            case ECMakeAttrScope::Public:
                dest = curCmakeTarget.InterfaceTarget ? &curCmakeTarget.Attributes[attrMacro].Iface : &curCmakeTarget.Attributes[attrMacro].Pub;
            break;
            case ECMakeAttrScope::Private:
                dest = curCmakeTarget.InterfaceTarget ? nullptr : &curCmakeTarget.Attributes[attrMacro].Priv;
            break;
            }

            if (dest) {
                for (const auto& val: vals) {
                    dest->emplace_back(ConvertArg(val));
                }
            }
        }

        void SetTargetMacroArgs(TVector<std::string> args) {
            if (!CurTarget_) {
                spdlog::error("attempt to set target macro args while there is no active target");
                return;
            }
            CurTarget_->MacroArgs = std::move(args);
        }

        void AddTargetAttrs(const std::string& attrMacro, const TAttrValues& values) {
            if (!CurTarget_) {
                spdlog::error("attempt to add target attribute '{}' while there is no active target", attrMacro);
                return;
            }
            auto& curCmakeTarget = *CurTarget_.As<TCMakeTarget>();
            auto& destAttrs = curCmakeTarget.Attributes[attrMacro];
            Copy(values.Iface.begin(), values.Iface.end(), std::back_inserter(destAttrs.Iface));
            Copy(values.Pub.begin(), values.Pub.end(), std::back_inserter(curCmakeTarget.InterfaceTarget ? destAttrs.Iface : destAttrs.Pub));
            if (curCmakeTarget.InterfaceTarget) {
                Copy(values.Priv.begin(), values.Priv.end(), std::back_inserter(destAttrs.Priv));
            }
        }

        void AddDirMacro(const std::string& name, std::span<const std::string> args) {
            if (!CurSubdir_) {
                spdlog::error("attempt to add macro '{}' while there is no active CMakeLists.txt", name);
                return;
            }
            CurSubdir_.As<TCMakeList>()->Macros.emplace_back(name, ConvertMacroArgs(args));
        }

        void AddTargetMacro(const std::string& name, std::span<const std::string> args, EMacroMergePolicy mergePolicy) {
            if (!CurTarget_) {
                spdlog::error("attempt to add target attribute '{}' while there is no active target", name);
                return;
            }
            auto& curCmakeTarget = *CurTarget_.As<TCMakeTarget>();
            switch (mergePolicy) {
                case EMacroMergePolicy::ConcatArgs: {
                    auto pos = curCmakeTarget.Macros.find(name);
                    if (pos == curCmakeTarget.Macros.end())
                        pos = curCmakeTarget.Macros.insert({name, {}});
                    Copy(args.begin(), args.end(), std::back_inserter(pos->second));
                    break;
                }
                case EMacroMergePolicy::FirstCall:
                    if (curCmakeTarget.Macros.contains(name))
                        break;
                    [[fallthrough]];
                case EMacroMergePolicy::MultipleCalls:
                    curCmakeTarget.Macros.emplace(name, ConvertMacroArgs(args)); break;
            }
        }

        // at directory level but should logically be grouped with the current target
        void AddTargetDirMacro(const std::string& name, std::span<const std::string> args) {
            if (!CurTarget_) {
                spdlog::error("attempt to add target directory macro '{}' while there is no active target", name);
                return;
            }
            CurTarget_.As<TCMakeTarget>()->DirMacros.emplace_back(name, ConvertMacroArgs(args));
        }

        bool SupportsAllocator() const {
            if (!CurTarget_) {
                spdlog::error("attempt to check support allocator while there is no active target");
                return false;
            }
            // Ugly hack here. In Jinja generator world PROGRAM and DLL target should use their own templates
            // and handle allocators in a different way then other target types
            return CurTarget_->Macro == "add_executable"sv || (CurTarget_->Macro == "add_library" && !CurTarget_->MacroArgs.empty() && CurTarget_->MacroArgs[0] == "SHARED"sv);
        }

        void AddGlobalVar(const std::string& name, std::span<const std::string> args) {
            auto& platformGlobalVars = Platform->GlobalVars;
            if (args.empty() || platformGlobalVars.contains(name)) {
                return;
            }
            TVector<std::string> macroArgs;
            macroArgs.reserve(args.size());
            for (const auto& arg : args) {
                macroArgs.emplace_back(ConvertArg(arg));
            }
            platformGlobalVars.emplace(name, macroArgs);
        }

        void AddLanguage(const std::string& name) {
            GlobalProperties.Languages.insert(name);
        }

        void RequireConanPackage(const std::string& name) {
            GlobalProperties.ConanPackages.insert(name);
        }

        void RequireConanToolPackage(const std::string& name) {
            GlobalProperties.ConanToolPackages.insert(name);
        }

        void ImportConanArtefact(const std::string& importSpec) {
            GlobalProperties.ConanImports.insert(importSpec);
        }

        void AddConanOption(const std::string& optSpec) {
            GlobalProperties.ConanOptions.insert(optSpec);
        }

        void SetVanillaProtobuf(const bool vanillaProtobuf) {
            GlobalProperties.VanillaProtobuf = vanillaProtobuf;
        }

        void AppendTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget_.As<TCMakeTarget>()->Properties[name];
            for (const auto& val: values) {
                prop.emplace_back(val);
            }
        }

        void SetTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget_.As<TCMakeTarget>()->Properties[name];
            prop.clear();
            for (const auto& val: values) {
                prop.emplace_back(val);
            }
        }

        void AddArcadiaScript(const fs::path& path) {
            GlobalProperties.ArcadiaScripts.insert(path.lexically_relative(ArcadiaScriptsRoot));
        }

        void UseGlobalFindModule(const std::string& pkg) {
            fs::path path = fmt::format("cmake/Find{}.cmake", pkg);
            auto exportFileManager = CMakeGenerator->GetExportFileManager();
            const auto& generatorDir = CMakeGenerator->GetGeneratorDir();
            exportFileManager->Copy(generatorDir / path, path);
        }

        void FindPackage(const std::string& name, TSet<std::string> components) {
            if (!CurSubdir_) {
                spdlog::error("attempt to add find_package macro while there is no active CMakeLists.txt");
                return;
            }
            CurSubdir_.As<TCMakeList>()->Packages[name].insert(components.begin(), components.end());
        }

        void Include(const std::string& mod) {
            if (!CurSubdir_) {
                spdlog::error("attempt to add find_package macro while there is no active CMakeLists.txt");
                return;
            }
            CurSubdir_.As<TCMakeList>()->Includes.emplace_back(mod);
        }

        void SetFakeFlag(bool isFake) {
            if (CurTarget_) {
                CurTarget_.As<TCMakeTarget>()->InterfaceTarget = isFake;
            }
        }

        const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
            return CMakeGenerator->ApplyReplacement(path, inputSem);
        }

    private:
        TVector<std::string> ConvertMacroArgs(std::span<const std::string> args) {
            TVector<std::string> macroArgs;
            macroArgs.reserve(args.size());
            for (const auto& arg : args) {
                macroArgs.emplace_back(ConvertArg(arg));
            }
            return macroArgs;
        }

        std::string ConvertArg(std::string arg) {
            if (arg.starts_with(YMAKE_SOURCE_PREFIX)) {
                return fmt::format("${{PROJECT_SOURCE_DIR}}/{}", arg.substr(YMAKE_SOURCE_PREFIX.size()));
            } else if (arg.starts_with(YMAKE_BUILD_PREFIX)) {
                return fmt::format("${{PROJECT_BINARY_DIR}}/{}", arg.substr(YMAKE_BUILD_PREFIX.size()));
            } else if (arg == "$B") {
                return "${PROJECT_BINARY_DIR}";
            } else if (arg == "$S") {
                return "${PROJECT_SOURCE_DIR}";
            } else if (!Tools.empty()) {
                TStringBuf toolRelPath;
                if (TStringBuf(arg).AfterPrefix("${PROJECT_BINARY_DIR}/"sv, toolRelPath)) {
                    TStringBuf arcadiaPath;
                    TStringBuf toolName;
                    SplitToolPath(TStringBuf(toolRelPath), arcadiaPath, toolName);
                    auto it = Tools.find(toolName);
                    if (it != Tools.end()) {
                        if (it->second != arcadiaPath) {
                            spdlog::error(
                                "toolName '{}': found tool in args with the same name but different Arcadia path.\n"
                                "  Path in Tools: {}, Path in arg: {}",
                                toolName,
                                it->second,
                                arcadiaPath
                            );
                        } else {
                            CurTarget_.As<TCMakeTarget>()->HostToolExecutableTargetDependencies.insert(TString(toolName));
                            return fmt::format("${{{}}}", GetToolBinVariable(toolName));
                        }
                    }
                }
            }
            return arg;
        }

    private:
        const TPlatformPtr Platform;
        TGlobalProperties& GlobalProperties;
        TCMakeGenerator* CMakeGenerator;

        // target_name -> path_in_arcadia
        TMap<std::string_view, std::string_view> Tools;
    };

    struct TExtraStackData {
        TProject::TBuilder::TTargetHolder CurTargetHolder;
        bool FreshNode = false;
    };

    static const std::string PROPERTY{"PROPERTY"};

    class TCmakeRenderingVisitor : public TGraphVisitor {
    public:
        TCmakeRenderingVisitor(const TPlatformPtr platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
            : TGraphVisitor(cmakeGenerator)
        {
            CMakeProjectBuilder_ = MakeSimpleShared<TCMakeProject::TBuilder>(platform, globalProperties, cmakeGenerator);
            ProjectBuilder_ = CMakeProjectBuilder_;
            for (const auto& targetSemantic : KnownTargets) {
                AddSemanticMapping(targetSemantic, ESNT_Target);
            }
        }

    protected:
        std::optional<bool> OnEnter(TState& state) override {
            if (state.TopNode()->NodeType == EMNT_File) {
                const auto rootrel = NPath::CutAllTypes(state.TopNode().Value().Path);
                if (NPath::IsPrefixOf(ArcadiaScriptsRoot, rootrel)) {
                    CMakeProjectBuilder_->AddArcadiaScript(fs::path::string_type{rootrel});
                    return true;
                }
            }
            MineToolPaths(state.TopNode());
            return {};
        }

        void OnTargetNodeSemantic(TState& state, const std::string& semName, const std::span<const std::string>& semArgs) override {
            const TSemNodeData& data = state.TopNode().Value();
            state.Top().CurTargetHolder = ProjectBuilder_->CreateTarget(semArgs[0].c_str());
            auto* curTarget = ProjectBuilder_->CurrentTarget();
            auto macroArgs = semArgs.subspan(2);
            curTarget->Name = semArgs[1];
            curTarget->Macro = semName;
            curTarget->MacroArgs = {macroArgs.begin(), macroArgs.end()};
            curTarget->Attrs = Generator_->MakeAttrs(EAttrGroup::Target, "target " + curTarget->Macro + " " + curTarget->Name);

            Mod2Target.emplace(state.TopNode().Id(), ProjectBuilder_->CurrentTarget());
            // TODO(svidyuk) populate target props on post order part of the traversal and keep track locally used tools only instead of global dict
            TargetsDict.emplace(NPath::CutType(data.Path), ProjectBuilder_->CurrentTarget());
        }

        void OnNodeSemanticPreOrder(TState& state, const std::string& semName, ESemNameType, const std::span<const std::string>& semArgs) override {
            const TSemNodeData& data = state.TopNode().Value();
            if (KnownAttrs.contains(semName)) {
                if (!semArgs.empty()) {
                    CMakeProjectBuilder_->AddTargetAttr(semName, ParseCMakeScope(semArgs[0]), semArgs.subspan(1));
                } else {
                    spdlog::error("attribute {} requires arguments SCOPE and VALUES..., provided arguments: '{}'. File '{}'", semName, fmt::join(semArgs, " "), data.Path);
                }
            }
            else if (KnownTargetMacro.contains(semName)) {
                CMakeProjectBuilder_->AddTargetMacro(semName, semArgs, EMacroMergePolicy::MultipleCalls);
            }
            else if (KnownUniqueTargetMacro.contains(semName)) {
                CMakeProjectBuilder_->AddTargetMacro(semName, semArgs, EMacroMergePolicy::FirstCall);
            }
            else if (KnownDirMacro.contains(semName)) {
                CMakeProjectBuilder_->AddTargetDirMacro(semName, semArgs);
            }
            else if (semName == "conan_require") {
                if (!CheckArgs(semName, semArgs, Exact(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->RequireConanPackage(semArgs[0]);
            }
            else if (semName == "conan_require_tool") {
                if (!CheckArgs(semName, semArgs, Exact(1), data.Path)){
                    return;
                }
                CMakeProjectBuilder_->RequireConanToolPackage(semArgs[0]);
            }
            else if (semName == "conan_import") {
                if (!CheckArgs(semName, semArgs, Exact(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->ImportConanArtefact(semArgs[0]);
            }
            else if (semName == "conan_options") {
                for (const auto& arg : semArgs) {
                    CMakeProjectBuilder_->AddConanOption(arg);
                }
            }
            else if (semName == "append_target_property") {
                if (!CheckArgs(semName, semArgs, MoreThan(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->AppendTargetProperty(semArgs[0], semArgs.subspan(1));
            }
            else if (semName == "set_target_property") {
                if (!CheckArgs(semName, semArgs, MoreThan(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->SetTargetProperty(semArgs[0], semArgs.subspan(1));
            }
            else if (semName == "include") {
                if (!CheckArgs(semName, semArgs, Exact(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->Include(semArgs[0]);
            }
            else if (semName == "find_package") {
                if (semArgs.size() != 1 && !(semArgs.size() > 2 && semArgs[1] == "COMPONENTS")) {
                    spdlog::error("wrong find_package arguments: {}\n\tPosible signatures:\n\t\t* find_package(<name>)\n\t\t* find_package(<name> COMPONENTS <component>+)", fmt::join(semArgs, ", "));
                    OnError();
                    return;
                }

                const auto& pkg = semArgs[0];
                const auto components = semArgs.size() > 1 ? semArgs.subspan(2) : std::span<std::string>{};
                if (IsModule(state.Top())) {
                    InducedCmakePackages[state.TopNode().Id()].insert({pkg, {components.begin(), components.end()}});
                } else {
                    CMakeProjectBuilder_->FindPackage(pkg, {components.begin(), components.end()});
                }
                if (BundledFindModules.contains(pkg)) {
                    CMakeProjectBuilder_->UseGlobalFindModule(pkg);
                }
            }
            else if (semName == "consumer_link_library") {
                if (!CheckArgs(semName, semArgs, AtLeast(2), data.Path)) {
                    return;
                }
                switch (ParseCMakeScope(semArgs.front())) {
                    case ECMakeAttrScope::Interface:
                        Copy(std::next(semArgs.begin()), semArgs.end(), std::back_inserter(InducedLinkLibs[state.TopNode().Id()].Iface));
                        break;
                    case ECMakeAttrScope::Public:
                        Copy(std::next(semArgs.begin()), semArgs.end(), std::back_inserter(InducedLinkLibs[state.TopNode().Id()].Pub));
                        break;
                    case ECMakeAttrScope::Private:
                        Copy(std::next(semArgs.begin()), semArgs.end(), std::back_inserter(InducedLinkLibs[state.TopNode().Id()].Priv));
                        break;
                }
            }
            else if (semName == "target_allocator") {
                if (!CheckArgs(semName, semArgs, AtLeast(2), data.Path)) {
                    return;
                }
                // Skip scope for allocator since it's always PRIVATE dep of executable
                Copy(std::next(semArgs.begin()), semArgs.end(), std::back_inserter(InducedAllocator[state.TopNode().Id()]));
            }
            else if (semName == "set_global_flags") {
                if (semArgs.size() > 1) {
                    CMakeProjectBuilder_->AddGlobalVar(semArgs[0], semArgs.subspan(1));
                }
            }
            else if (semName == "library_fake_marker") {
                if (!GetFakeModuleFlag(semName, semArgs)) {
                    CMakeProjectBuilder_->SetFakeFlag(false);
                    return;
                }
                if (AnyOf(state.TopNode().Edges(), [](const auto& dep) { return IsModuleOwnNodeDep(dep) && !IsGlobalSrcDep(dep); })) {
                    std::string linkerLangs[] = {"CXX"};
                    CMakeProjectBuilder_->AppendTargetProperty("LINKER_LANGUAGE", linkerLangs);
                    CMakeProjectBuilder_->SetTargetMacroArgs({"STATIC"});
                } else {
                    CMakeProjectBuilder_->SetFakeFlag(true);
                }
            }
            else if (semName == "target_proto_plugin") {
                // The code in this branch is plain disaster and more transparent and generic way of turining tool reference to target name
                // is required.
                if (!CheckArgs(semName, semArgs, Exact(2), data.Path)){
                    return;
                }
                // Ensure tools are traversed and added to Targets dict.
                for (const auto& dep : state.TopNode().Edges()) {
                    if (IsBuildCommandDep(dep) && dep.To()->Path == TOOL_NODES_FAKE_PATH) {
                        IterateAll(state, dep.To(), *this);
                    }
                }
                // ${tool;rootrel:...} is broken :(
                const auto fres = TargetsDict.find(std::string_view(semArgs[1]).substr("${PROJECT_BINARY_DIR}/"sv.size()));
                if (fres == TargetsDict.end()) {
                    const auto* curTarget = ProjectBuilder_->CurrentTarget();
                    spdlog::error("No proto plugin tool found '{}' for target '{}'", semArgs[1], curTarget ? curTarget->Name : "NO_TARGET");
                    return;
                }
                const std::string patchedArgs[] = {semArgs[0], fres->second->Name};
                CMakeProjectBuilder_->AddTargetMacro(semName, patchedArgs, EMacroMergePolicy::MultipleCalls);
            }
            else if (semName == "curdir_masm_flags") {
                if (semArgs.empty()) {
                    return;
                }
                CMakeProjectBuilder_->AddDirMacro("curdir_masm_flags", semArgs);
            }
            else if (semName == "set_vars") {
                for (std::string_view arg : semArgs) {
                    const auto pos = arg.find("=");
                    if (pos >= arg.size())
                        throw yexception() << "Bad var def: " << arg;
                    std::string setArgs[] = {std::string{arg.substr(0, pos)}, std::string{arg.substr(pos + 1)}};
                    CMakeProjectBuilder_->AddDirMacro("set", setArgs);
                }
            }
            else if (semName == "add_language") {
                if (!CheckArgs(semName, semArgs, Exact(1), data.Path)) {
                    return;
                }
                CMakeProjectBuilder_->AddLanguage(semArgs[0]);
            }
            else if (semName == "add_requirements" || semName == "add_ytest_requirements" || semName == "add_test_requirements") {
                const auto set_prop = semName == "add_test_requirements" ? "set_property"s : "set_yunittest_property"s;
                // add_requirements $REALPRJNAME $DEFAULT_REQUIREMENTS $TEST_REQUIREMENTS_VALUE
                if (!CheckArgs(semName, semArgs, AtLeast(2), data.Path)) {
                    return;
                }
                std::string nproc;
                for (std::string_view arg : semArgs.subspan(1)) {
                    const auto pos = arg.find("cpu:");
                    if (pos == 0 && arg.size() > 4) {
                        nproc = arg.substr(4);
                    }
                }
                if (!nproc.empty()) {
                    std::string setArgs[] = {"TEST", semArgs[0], PROPERTY, "PROCESSORS", nproc};
                    CMakeProjectBuilder_->AddTargetDirMacro(set_prop, setArgs);
                }
            }
            else if (semName == "set_property_escaped" || semName == "set_yunittest_property_escaped") {
                const auto set_prop = semName == "set_property_escaped" ? "set_property"s : "set_yunittest_property"s;
                if (!CheckArgs(semName, semArgs, AtLeast(2), data.Path)) {
                    return;
                }
                auto it = std::find(semArgs.begin(), semArgs.end(), PROPERTY);
                if (it == semArgs.end() || (it + 3) >= semArgs.end()) {
                    spdlog::error("Can't find {} or values in '{}' semantic at node {}, set unescaped", PROPERTY, semName, data.Path);
                    CMakeProjectBuilder_->AddTargetDirMacro(set_prop, semArgs);
                } else {
                    std::vector<std::string> setArgs(semArgs.begin(), it + 2);
                    setArgs.emplace_back(QuoteAndEscapeValues(semArgs.subspan(it - semArgs.begin() + 2)));
                    CMakeProjectBuilder_->AddTargetDirMacro(set_prop, setArgs);
                }
            }
            else if (semName == "vanilla_protobuf") {
                CMakeProjectBuilder_->SetVanillaProtobuf(true);
            }
            else {
                spdlog::error("Unknown semantic '{}' for file '{}'", semName, data.Path);
            }
            return;
        }

        void OnLeave(TState& state) override {
            if (state.Top().FreshNode && state.HasIncomingDep()) {
                const auto dep = state.IncomingDep();
                if (IsGlobalSrcDep(dep)) {
                    // Note: There is assumption here that all PEERDIR nodes are traversed before GlobalSrcDeps
                    const auto fres = Mod2Target.find(dep.From().Id());
                    if (fres != Mod2Target.end()) {
                        const auto& cmakeTarget = *dynamic_cast<const TCMakeTarget*>(fres->second);
                        const auto linkLibs = cmakeTarget.Attributes.find("target_link_libraries");
                        if (linkLibs != cmakeTarget.Attributes.end()) {
                            TAttrValues libs;
                            Copy(linkLibs->second.Iface.begin(), linkLibs->second.Iface.end(), std::back_inserter(libs.Pub));
                            Copy(linkLibs->second.Pub.begin(), linkLibs->second.Pub.end(), std::back_inserter(libs.Pub));
                            CMakeProjectBuilder_->AddTargetAttrs("target_link_libraries", libs);
                        }
                    } else {
                        spdlog::error(
                            "Main target not found for global target '{}'. Most likely main target ya.make module is not intended for opensource export",
                            CMakeProjectBuilder_->CurrentTarget()->Name
                        );
                    }
                }
            }
        }

        void OnLeft(TState& state) override {
            const auto& dep = state.Top().CurDep();
            // NODE(svidyuk): IsGlobalSrcDep check here assumes GLOBAL_CMD is used rather than individual object files propagation.
            if (IsDirectPeerdirDep(dep) || IsGlobalSrcDep(dep)) {
                const auto pkgIt = InducedCmakePackages.find(dep.To().Id());
                if (pkgIt != InducedCmakePackages.end()) {
                    for (const auto& [pkg, components]: pkgIt->second) {
                        CMakeProjectBuilder_->FindPackage(pkg, components);
                    }
                }

                const auto libIt = InducedLinkLibs.find(dep.To().Id());
                if (libIt != InducedLinkLibs.end()) {
                    CMakeProjectBuilder_->AddTargetAttrs("target_link_libraries", libIt->second);
                }
                const auto allocIt = InducedAllocator.find(dep.To().Id());
                if (allocIt != InducedAllocator.end()) {
                    if (CMakeProjectBuilder_->SupportsAllocator()) {
                        CMakeProjectBuilder_->AddTargetMacro("target_allocator", allocIt->second, EMacroMergePolicy::ConcatArgs);
                    } else {
                        const auto* curTarget = CMakeProjectBuilder_->CurrentTarget();
                        const auto& cmakeTarget = *dynamic_cast<const TCMakeTarget*>(curTarget);

                        CMakeProjectBuilder_->AddTargetAttr(
                            "target_link_libraries",
                            curTarget && cmakeTarget.InterfaceTarget ? ECMakeAttrScope::Interface : ECMakeAttrScope::Public,
                            allocIt->second
                        );
                    }
                }
            }
        }

    private:
        bool GetFakeModuleFlag(const std::string& name, std::span<const std::string> args) {
            if (args.empty()) {
                spdlog::error("attribute {} is empty.", name);
                return false;
            }
            if (args[0] != "FAKE_MODULE" || args.size() != 2) {
                return false;
            }
            if (IsTrue(args[1])) {
                return true;
            } else if (IsFalse(args[1])) {
                return false;
            }

            spdlog::error("invalid value for FAKE_MODULE argument: {}", args[1]);
            return false;
        }
        void AddTool(TStringBuf arcadiaPath, TStringBuf toolName) {
            std::string args[] = {
                GetToolBinVariable(toolName),
                fmt::format("TOOL_{}_dependency", toolName),
                std::string(arcadiaPath),
                std::string(toolName)};
            CMakeProjectBuilder_->AddDirMacro(
                "get_built_tool_path",
                args
            );
        }

        void MineToolPaths(const TSemGraph::TConstNodeRef& node)  {
            TMap<std::string_view, std::string_view> tools;

            for (const auto& dep: node.Edges()) {
                if (!IsBuildCommandDep(dep) || dep.To()->Path != TOOL_NODES_FAKE_PATH) {
                    continue;
                }

                if (IsIgnored(dep.To())) {
                    continue;
                }

                for (const auto& tool: dep.To().Edges()) {
                    if (IsIgnored(tool.To())) {
                        continue;
                    }
                    TStringBuf arcadiaPath;
                    TStringBuf toolName;
                    SplitToolPath(TStringBuf(NPath::CutType(tool.To()->Path)), arcadiaPath, toolName);
                    if (tools.contains(toolName)) {
                        spdlog::error("duplicate tool name '{}'", toolName);
                        return;
                    }
                    AddTool(arcadiaPath, toolName);
                    tools.emplace(toolName, arcadiaPath);
                }
            }
            CMakeProjectBuilder_->SetCurrentTools(std::move(tools));
        }

        TSimpleSharedPtr<TCMakeProject::TBuilder> CMakeProjectBuilder_;

        THashMap<TNodeId, TInducedCmakePackages> InducedCmakePackages;
        THashMap<TNodeId, TAttrValues> InducedLinkLibs;
        THashMap<TNodeId, TVector<std::string>> InducedAllocator;

        THashMap<TStringBuf, const TProjectTarget*> TargetsDict;
        THashMap<TNodeId, const TProjectTarget*> Mod2Target;

        THashSet<std::string> KnownTargets = {
            "add_executable",
            "add_global_library_for",
            "add_library",
            "add_shared_library",
            "add_fat_object",
            "add_recursive_library",
            "add_swig_jni_library"
        };
        THashSet<TStringBuf> KnownAttrs = {
            "target_compile_options",
            "target_include_directories",
            "target_link_libraries",
            "target_link_options",
            "target_ev_messages",
            "target_proto_messages",
            "target_sources",
            "target_cuda_sources",
            "target_cython_sources",
        };
        THashSet<TStringBuf> KnownTargetMacro = {
            "generate_enum_serilization",
            "target_joined_source",
            "target_ragel_lexers",
            "target_fbs_source",
            "target_flex_lexers",
            "target_bison_parser",
            "target_yasm_source",
            "target_rodata_sources",
            "target_sources_custom",
            "vcs_info",
            "resources",
            "llvm_compile_c",
            "llvm_compile_cxx",
            "archive"
        };
        THashSet<TStringBuf> KnownUniqueTargetMacro = {
            "target_cuda_flags",
            "target_cuda_cflags",
            "target_proto_outs",
            "target_proto_addincls",
            "use_export_script",
            "target_cython_options",
            "target_cython_include_directories",
            "swig_java_sources",
            "set_python_type_for_cython"
        };
        THashSet<TStringBuf> KnownDirMacro = {
            "add_custom_command",
            "add_test",
            "add_yunittest",
            "set_yunittest_property",
            "copy_file",
            "configure_file",
            "find_file",
            "run_antlr",
            "set_property",
            "add_jar"
        };
        THashSet<TString> BundledFindModules = {"AIO", "IDN", "JNITarget"};
    };
}

bool AnalizePlatformSemGraph(const TPlatformPtr platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator) {
    TCmakeRenderingVisitor visitor(platform, globalProperties, cmakeGenerator);
    visitor.FillPlatformName(platform->Name);
    IterateAll(*platform->Graph, platform->StartDirs, visitor);
    platform->Project = visitor.TakeFinalizedProject();
    return !visitor.HasErrors();
}

TAttrsPtr GetSubdirValuesMap(const TPlatformPtr platform, TProjectSubdirPtr subdir, const TCMakeGenerator* cmakeGenerator) {
    return platform->Project.As<TCMakeProject>()->GetSubdirValuesMap(*subdir.As<TCMakeList>(), cmakeGenerator);
}

THashMap<fs::path, TSet<fs::path>> GetSubdirsTable(const TPlatformPtr platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
{
    TCmakeRenderingVisitor visitor(platform, globalProperties, cmakeGenerator);
    IterateAll(*platform->Graph, platform->StartDirs, visitor);
    auto project = visitor.TakeFinalizedProject();
    return project.As<TCMakeProject>()->GetSubdirsTable();
}

}
