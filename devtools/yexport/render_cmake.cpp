#include "render_cmake.h"
#include "cmake_generator.h"
#include "std_helpers.h"
#include "project.h"

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

    enum class EConstraintType {
        AtLeast,
        MoreThan,
        Exact
    };
    struct TArgsConstraint {
        EConstraintType Type;
        size_t Count;
    };

    TArgsConstraint AtLeast(size_t count) {
        return {
            .Type = EConstraintType::AtLeast,
            .Count = count
        };
    }
    TArgsConstraint MoreThan(size_t count) {
        return {
            .Type = EConstraintType::MoreThan,
            .Count = count
        };
    }
    TArgsConstraint Exact(size_t count) {
        return {
            .Type = EConstraintType::Exact,
            .Count = count
        };
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

        TCMakeProject(const TProjectConf& projectConf, TPlatform& platform, TExportFileManager* exportFileManager)
        : TProject()
        , ProjectConf(projectConf)
        , Platform(platform)
        , ExportFileManager(exportFileManager)
        {
            SetFactoryTypes<TCMakeList, TCMakeTarget>();
        }

        void Save() {
            // const auto dest
            THashSet<fs::path> platformDirs;
            auto out = ExportFileManager->Open(Platform.Conf.CMakeListsFile);
            fmt::memory_buffer buf;
            auto bufIt = std::back_inserter(buf);

            fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);

            for (auto subdir: SubdirsOrder_) {
                Y_ASSERT(subdir);
                Platform.SubDirs.insert(subdir->Path);
                if (subdir->IsTopLevel()) {
                    fmt::format_to(bufIt, "add_subdirectory({})\n", subdir->Path.c_str());
                }
                SaveSubdirCMake(subdir->Path, *subdir.As<TCMakeList>());
            }

            out.Write(buf.data(), buf.size());
        }

        THashMap<fs::path, TSet<fs::path>> GetSubdirsTable() const {
            THashMap<fs::path, TSet<fs::path>> result;
            for (auto subdir: SubdirsOrder_) {
                result.insert({subdir->Path, subdir.As<TCMakeList>()->SubdirectoriesToAdd});
            }
            return result;
        }

    private:
        static void PrintDirLevelMacros(auto& bufIt, const TVector<std::pair<std::string, TVector<std::string>>>& macros) {
            for (const auto& [macro, values]: macros) {
                fmt::format_to(bufIt, "{}(\n  {}\n)\n", macro, fmt::join(values, "\n  "));
            }
        }

        void SaveSubdirCMake(const fs::path& subdir, const TCMakeList& data) {
            auto out = ExportFileManager->Open(subdir / Platform.Conf.CMakeListsFile);
            fs::path epilogue{ProjectConf.ArcadiaRoot/subdir/CMakeEpilogueFile};
            fs::path prologue{ProjectConf.ArcadiaRoot/subdir/CMakePrologueFile};

            fmt::memory_buffer buf;
            auto bufIt = std::back_inserter(buf);
            fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);

            for (const auto& [name, components]: data.Packages) {
                if (components.empty()) {
                    fmt::format_to(bufIt, "find_package({} REQUIRED)\n", name);
                } else {
                    fmt::format_to(bufIt, "find_package({} REQUIRED COMPONENTS {})\n", name, fmt::join(components, " "));
                }
            }

            {
            THashSet<std::string_view> dedup;
                for (const auto& mod: data.Includes) {
                    if (dedup.insert(mod).second) {
                        fmt::format_to(bufIt, "include({})\n", mod);
                    }
                }
            }

            // add subdirectories that contain targets inside (possibly several levels lower)
            for (const auto& addSubdir: data.SubdirectoriesToAdd) {
                fmt::format_to(bufIt, "add_subdirectory({})\n", addSubdir.c_str());
            }

            PrintDirLevelMacros(bufIt, data.Macros);

            for (auto tgt: data.Targets) {
                const auto& cmakeTarget = *tgt.As<TCMakeTarget>();
                if (buf.size() != 0) {
                    fmt::format_to(bufIt, "\n");
                }
                // Target definition
                if (cmakeTarget.InterfaceTarget) {
                    fmt::format_to(bufIt, "{}({} INTERFACE", tgt->Macro, tgt->Name);
                } else {
                    fmt::format_to(bufIt, "{}({}", tgt->Macro, tgt->Name);
                }
                if (tgt->MacroArgs.empty()) {
                    fmt::format_to(bufIt, ")\n");
                } else if (std::accumulate(tgt->MacroArgs.begin(), tgt->MacroArgs.end(), 0u, [](size_t cnt, auto&& arg) {return cnt + arg.size();}) > 100) {
                    fmt::format_to(bufIt, "\n  {}\n)\n", fmt::join(tgt->MacroArgs, "\n  "));
                } else {
                    fmt::format_to(bufIt, " {})\n", fmt::join(tgt->MacroArgs, " "));
                }
                // Target properties
                for (const auto& [name, values]: cmakeTarget.Properties) {
                    if (values.empty()) {
                        continue;
                    }
                    fmt::format_to(bufIt, "set_property(TARGET {} PROPERTY\n  {} {}\n)\n", tgt->Name, name, fmt::join(values, " "));
                }
                // Target attributes
                for (const auto& [macro, values]: cmakeTarget.Attributes) {
                    if (!values.Iface.empty()) {
                        fmt::format_to(bufIt, "{}({} INTERFACE\n  {}\n)\n", macro, tgt->Name, fmt::join(values.Iface, "\n  "));
                    }
                    if (!values.Pub.empty()) {
                        fmt::format_to(bufIt, "{}({} PUBLIC\n  {}\n)\n", macro, tgt->Name, fmt::join(values.Pub, "\n  "));
                    }
                    if (!values.Priv.empty()) {
                        fmt::format_to(bufIt, "{}({} PRIVATE\n  {}\n)\n", macro, tgt->Name, fmt::join(values.Priv, "\n  "));
                    }
                }

                // Print target-related dir level macros
                PrintDirLevelMacros(bufIt, cmakeTarget.DirMacros);

                // Target level macros
                for (const auto& [macro, values]: cmakeTarget.Macros) {
                    if (values.empty()) {
                        fmt::format_to(bufIt, "{}({})\n", macro, tgt->Name);
                    } else {
                        fmt::format_to(bufIt, "{}({}\n  {}\n)\n", macro, tgt->Name, fmt::join(values, "\n  "));
                    }
                }

                // Dependencies
                if (!cmakeTarget.HostToolExecutableTargetDependencies.empty()) {
                    fmt::format_to(
                        bufIt,
                        "if(NOT CMAKE_CROSSCOMPILING)\n  add_dependencies({}\n    {}\n)\nendif()\n",
                        tgt->Name,
                        fmt::join(cmakeTarget.HostToolExecutableTargetDependencies, "\n    ")
                    );
                }
            }

            auto writeToFile = [&out](const fs::path& path) {
                TFileInput fileInput(TFile{path, RdOnly});
                TString fileData = fileInput.ReadAll();
                out.Write(fileData.data(), fileData.size());
                out.Write("\n", 1);
            };

            if (fs::exists(prologue)) {
                writeToFile(prologue);
            }

            out.Write(buf.data(), buf.size());

            if (fs::exists(epilogue)) {
                writeToFile(epilogue);
            }
        }

    private:
        static constexpr std::string_view CMakeEpilogueFile = "epilogue.cmake";
        static constexpr std::string_view CMakePrologueFile = "prologue.cmake";

    private:
        const TProjectConf& ProjectConf;
        TPlatform& Platform;
        TExportFileManager* ExportFileManager;
    };
    using TCMakeProjectPtr = TSimpleSharedPtr<TCMakeProject>;

    class TCMakeProject::TBuilder: public TProject::TBuilder {
    public:

        TBuilder(const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
        : TProject::TBuilder()
        , Platform(platform)
        , GlobalProperties(globalProperties)
        , CMakeGenerator(cmakeGenerator)
        {
            Project_ = MakeSimpleShared<TCMakeProject>(projectConf, platform, cmakeGenerator->GetExportFileManager());
        }

        void Finalize() override {
            for (auto subdir : Project_->GetSubdirs()) {
                auto& cmakeList = *subdir.As<TCMakeList>();
                for (const auto& child : cmakeList.Subdirectories) {
                    cmakeList.SubdirectoriesToAdd.insert(child->Path.filename());
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
                    dest->push_back(ConvertArg(val));
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
            CurSubdir_.As<TCMakeList>()->Macros.push_back({name, ConvertMacroArgs(args)});
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
            CurTarget_.As<TCMakeTarget>()->DirMacros.push_back({name, ConvertMacroArgs(args)});
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
            auto& platformGlobalVars = Platform.GlobalVars;
            if (args.empty() || platformGlobalVars.contains(name)) {
                return;
            }
            TVector<std::string> macroArgs;
            macroArgs.reserve(args.size());
            for (const auto& arg : args) {
                macroArgs.push_back(ConvertArg(arg));
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

        void AppendTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget_.As<TCMakeTarget>()->Properties[name];
            for (const auto& val: values) {
                prop.push_back(val);
            }
        }

        void SetTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget_.As<TCMakeTarget>()->Properties[name];
            prop.clear();
            for (const auto& val: values) {
                prop.push_back(val);
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
            CurSubdir_.As<TCMakeList>()->Includes.push_back(mod);
        }

        void SetFakeFlag(bool isFake) {
            if (CurTarget_) {
                CurTarget_.As<TCMakeTarget>()->InterfaceTarget = isFake;
            }
        }

        void AddSemantics(const std::string& semantica) {
            CMakeGenerator->OnAttribute(semantica);
            for (const auto& rulePtr : CMakeGenerator->GetGeneratorSpec().GetRules(semantica)) {
                const auto& addValues = rulePtr->AddValues;
                if (auto valuesIt = addValues.find("includes"); valuesIt != addValues.end()) {
                    for (const auto& value : valuesIt->second) {
                        GlobalProperties.GlobalModules.emplace(value);
                    }
                }
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
                macroArgs.push_back(ConvertArg(arg));
            }
            return macroArgs;
        }

        std::string ConvertArg(std::string arg) {
            if (arg.starts_with(YMAKE_SOURCE_PREFIX)) {
                return fmt::format("${{CMAKE_SOURCE_DIR}}/{}", arg.substr(YMAKE_SOURCE_PREFIX.size()));
            } else if (arg.starts_with(YMAKE_BUILD_PREFIX)) {
                return fmt::format("${{CMAKE_BINARY_DIR}}/{}", arg.substr(YMAKE_BUILD_PREFIX.size()));
            } else if (arg == "$B") {
                return "${CMAKE_BINARY_DIR}";
            } else if (arg == "$S") {
                return "${CMAKE_SOURCE_DIR}";
            } else if (!Tools.empty()) {
                TStringBuf toolRelPath;
                if (TStringBuf(arg).AfterPrefix("${CMAKE_BINARY_DIR}/"sv, toolRelPath)) {
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
        TPlatform& Platform;
        TGlobalProperties& GlobalProperties;
        TCMakeGenerator* CMakeGenerator;

        // target_name -> path_in_arcadia
        TMap<std::string_view, std::string_view> Tools;
    };

    struct TExtraStackData {
        TProject::TBuilder::TTargetHolder CurTargetHolder;
        bool FreshNode = false;
    };

    class TCmakeRenderingVisitor
        : public TNoReentryVisitorBase<
              TVisitorStateItemBase,
              TSemGraphIteratorStateItem<TExtraStackData>,
              TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>> {
    public:
        using TBase = TNoReentryVisitorBase<
            TVisitorStateItemBase,
            TSemGraphIteratorStateItem<TExtraStackData>,
            TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>>;
        using TState = typename TBase::TState;

        TCmakeRenderingVisitor(const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
        : ProjectBuilder(projectConf, platform, globalProperties, cmakeGenerator)
        {}

        bool Enter(TState& state) {
            state.Top().FreshNode = TBase::Enter(state);
            if (!state.Top().FreshNode) {
                return false;
            }

            if (state.TopNode()->NodeType == EMNT_File) {
                const auto rootrel = NPath::CutAllTypes(state.TopNode().Value().Path);
                if (NPath::IsPrefixOf(ArcadiaScriptsRoot, rootrel)) {
                    ProjectBuilder.AddArcadiaScript(fs::path::string_type{rootrel});
                    return true;
                }
            }

            const TSemNodeData& data = state.TopNode().Value();
            if (data.Sem.empty() || data.Sem.front().empty()) {
                return true;
            }

            MineToolPaths(state.TopNode());

            bool traverseFurther = true;
            for (const auto& sem: ProjectBuilder.ApplyReplacement(data.Path, data.Sem)) {
                if (sem.empty()) {
                    throw yexception() << "Empty semantic item on node '" << data.Path << "'";
                }
                // TODO(svidyuk): turn asserts bellow into normal error handling
                const auto& semName = sem[0];
                const auto semArgs = std::span{sem}.subspan(1);

                ProjectBuilder.AddSemantics(semName);

                if (semName == "IGNORED") {
                    traverseFurther = false;
                } else if (KnownTargets.contains(semName)) {
                    if (!CheckArgs(semName, semArgs, AtLeast(2)))
                        continue;

                    auto macroArgs = semArgs.subspan(2);
                    state.Top().CurTargetHolder = ProjectBuilder.CreateTarget(semArgs[0].c_str());
                    auto* curTarget = ProjectBuilder.CurrentTarget();
                    curTarget->Name = semArgs[1];
                    curTarget->Macro = semName;
                    curTarget->MacroArgs = {macroArgs.begin(), macroArgs.end()};

                    Mod2Target.emplace(state.TopNode().Id(), ProjectBuilder.CurrentTarget());
                    // TODO(svidyuk) populate target props on post order part of the traversal and keep track locally used tools only instead of global dict
                    TargetsDict.emplace(NPath::CutType(data.Path), ProjectBuilder.CurrentTarget());
                } else if (KnownAttrs.contains(semName)) {
                    if (!semArgs.empty()) {
                        ProjectBuilder.AddTargetAttr(semName, ParseCMakeScope(semArgs[0]), semArgs.subspan(1));
                    } else {
                        spdlog::error("attribute {} requires arguments SCOPE and VALUES..., provided arguments: '{}'. File '{}'", semName, fmt::join(semArgs, " "), data.Path);
                    }
                } else if (KnownTargetMacro.contains(semName)) {
                    ProjectBuilder.AddTargetMacro(semName, semArgs, EMacroMergePolicy::MultipleCalls);
                } else if (KnownUniqueTargetMacro.contains(semName)) {
                    ProjectBuilder.AddTargetMacro(semName, semArgs, EMacroMergePolicy::FirstCall);
                } else if (KnownDirMacro.contains(semName)) {
                    ProjectBuilder.AddTargetDirMacro(semName, semArgs);
                } else if (semName == "conan_require") {
                    if (!CheckArgs(semName, semArgs, Exact(1)))
                        continue;
                    ProjectBuilder.RequireConanPackage(semArgs[0]);
                } else if (semName == "conan_require_tool") {
                    if (!CheckArgs(semName, semArgs, Exact(1)))
                        continue;
                    ProjectBuilder.RequireConanToolPackage(semArgs[0]);
                } else if (semName == "conan_import") {
                    if (!CheckArgs(semName, semArgs, Exact(1)))
                        continue;
                    ProjectBuilder.ImportConanArtefact(semArgs[0]);
                } else if (semName == "conan_options") {
                    for (const auto& arg: semArgs) {
                        ProjectBuilder.AddConanOption(arg);
                    }
                } else if (semName == "append_target_property") {
                    if (!CheckArgs(semName, semArgs, MoreThan(1)))
                        continue;
                    ProjectBuilder.AppendTargetProperty(semArgs[0], semArgs.subspan(1));
                } else if (semName == "set_target_property") {
                    if (!CheckArgs(semName, semArgs, MoreThan(1)))
                        continue;
                    ProjectBuilder.SetTargetProperty(semArgs[0], semArgs.subspan(1));
                } else if (semName == "include") {
                    if (!CheckArgs(semName, semArgs, Exact(1)))
                        continue;
                    ProjectBuilder.Include(semArgs[0]);
                } else if (semName == "find_package") {
                    if (semArgs.size() != 1 && !(semArgs.size() > 2 && semArgs[1] == "COMPONENTS")) {
                        spdlog::error("wrong find_package arguments: {}\n\tPosible signatures:\n\t\t* find_package(<name>)\n\t\t* find_package(<name> COMPONENTS <component>+)", fmt::join(semArgs, ", "));
                        FoundErrors = true;
                        continue;
                    }

                    const auto& pkg = semArgs[0];
                    const auto components = semArgs.size() > 1 ? semArgs.subspan(2) : std::span<std::string>{};
                    if (IsModule(state.Top())) {
                        InducedCmakePackages[state.TopNode().Id()].insert({pkg, {components.begin(), components.end()}});
                    } else {
                        ProjectBuilder.FindPackage(pkg, {components.begin(), components.end()});
                    }
                    if (BundledFindModules.contains(pkg)) {
                        ProjectBuilder.UseGlobalFindModule(pkg);
                    }
                } else if (semName == "consumer_link_library") {
                    if (!CheckArgs(semName, semArgs, AtLeast(2)))
                        continue;
                    switch(ParseCMakeScope(semArgs.front())) {
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
                } else if (semName == "target_allocator") {
                    if (!CheckArgs(semName, semArgs, AtLeast(2)))
                        continue;
                    // Skip scope for allocator since it's always PRIVATE dep of executable
                    Copy(std::next(semArgs.begin()), semArgs.end(), std::back_inserter(InducedAllocator[state.TopNode().Id()]));
                } else if (semName == "set_global_flags") {
                    if (semArgs.size() > 1) {
                        ProjectBuilder.AddGlobalVar(semArgs[0], semArgs.subspan(1));
                    }
                } else if (semName == "library_fake_marker") {
                    if (!GetFakeModuleFlag(semName, semArgs)) {
                        ProjectBuilder.SetFakeFlag(false);
                        continue;
                    }
                    if (AnyOf(state.TopNode().Edges(), [](const auto& dep){ return IsModuleOwnNodeDep(dep) && !IsGlobalSrcDep(dep); })) {
                        std::string linkerLangs[] = {"CXX"};
                        ProjectBuilder.AppendTargetProperty("LINKER_LANGUAGE", linkerLangs);
                        ProjectBuilder.SetTargetMacroArgs({"STATIC"});
                    } else {
                        ProjectBuilder.SetFakeFlag(true);
                    }
                } else if (semName == "target_proto_plugin") {
                    // The code in this branch is plain disaster and more transparent and generic way of turining tool reference to target name
                    // is required.
                    if (!CheckArgs(semName, semArgs, Exact(2)))
                        continue;
                    // Ensure tools are traversed and added to Targets dict.
                    for (const auto& dep: state.TopNode().Edges()) {
                        if (IsBuildCommandDep(dep) && dep.To()->Path == TOOL_NODES_FAKE_PATH) {
                            IterateAll(state, dep.To(), *this);
                        }
                    }
                    // ${tool;rootrel:...} is broken :(
                    const auto fres = TargetsDict.find(std::string_view(semArgs[1]).substr("${CMAKE_BINARY_DIR}/"sv.size()));
                    if (fres == TargetsDict.end()) {
                        const auto* curTarget = ProjectBuilder.CurrentTarget();
                        spdlog::error("No proto plugin tool found '{}' for target '{}'", semArgs[1], curTarget ? curTarget->Name : "NO_TARGET");
                        return false;
                    }
                    const std::string patchedArgs[] = {semArgs[0], fres->second->Name};
                    ProjectBuilder.AddTargetMacro(semName, patchedArgs, EMacroMergePolicy::MultipleCalls);
                } else if (semName == "curdir_masm_flags") {
                    if (semArgs.empty())
                        continue;
                    ProjectBuilder.AddDirMacro("curdir_masm_flags", semArgs);
                } else if (semName == "set_vars") {
                    for (std::string_view arg: semArgs) {
                        const auto pos = arg.find("=");
                        if (pos >= arg.size())
                            throw yexception() << "Bad var def: " << arg;
                        std::string setArgs[] = {std::string{arg.substr(0, pos)}, std::string{arg.substr(pos+1)}};
                        ProjectBuilder.AddDirMacro("set", setArgs);
                    }
                } else if (semName == "add_language") {
                    if (!CheckArgs(semName, semArgs, Exact(1)))
                        continue;
                    ProjectBuilder.AddLanguage(semArgs[0]);
                } else if (semName == "add_requirements" || semName == "add_ytest_requirements" || semName == "add_test_requirements") {
                    const auto set_prop = semName == "add_test_requirements" ? "set_property"s : "set_yunittest_property"s;
                    // add_requirements $REALPRJNAME $DEFAULT_REQUIREMENTS $TEST_REQUIREMENTS_VALUE
                    if (!CheckArgs(semName, semArgs, AtLeast(2)))
                        continue;
                    std::string nproc;
                    for (std::string_view arg: semArgs.subspan(1)) {
                        const auto pos = arg.find("cpu:");
                        if (pos == 0 && arg.size() > 4) {
                            nproc = arg.substr(4);
                        }
                    }
                    if (!nproc.empty()) {
                        std::string setArgs[] = {"TEST", semArgs[0], "PROPERTY", "PROCESSORS", nproc};
                        ProjectBuilder.AddTargetDirMacro(set_prop, setArgs);
                    }
                } else {
                    spdlog::error("Unknown semantic '{}' for file '{}'", semName, data.Path);
                    return false;
                }
            }
            return traverseFurther;
        }

        void Leave(TState& state) {
            if (state.Top().FreshNode && state.HasIncomingDep()) {
                const auto dep = state.IncomingDep();
                if (IsGlobalSrcDep(dep)) {
                    // Note: There is assumption here that all PEERDIR nodes are traversed before GlobalSrcDeps
                    const auto fres = Mod2Target.find(dep.From().Id());
                    Y_ASSERT(fres != Mod2Target.end());
                    const auto& cmakeTarget = *dynamic_cast<const TCMakeTarget*>(fres->second);
                    const auto linkLibs = cmakeTarget.Attributes.find("target_link_libraries");
                    if (linkLibs != cmakeTarget.Attributes.end()) {
                        TAttrValues libs;
                        Copy(linkLibs->second.Iface.begin(), linkLibs->second.Iface.end(), std::back_inserter(libs.Pub));
                        Copy(linkLibs->second.Pub.begin(), linkLibs->second.Pub.end(), std::back_inserter(libs.Pub));
                        ProjectBuilder.AddTargetAttrs("target_link_libraries", libs);
                    }
                }
            }
            TBase::Leave(state);
        }

        void Left(TState& state) {
            const auto& dep = state.Top().CurDep();
            // NODE(svidyuk): IsGlobalSrcDep check here assumes GLOBAL_CMD is used rather than individual object files propagation.
            if (IsDirectPeerdirDep(dep) || IsGlobalSrcDep(dep)) {
                const auto pkgIt = InducedCmakePackages.find(dep.To().Id());
                if (pkgIt != InducedCmakePackages.end()) {
                    for (const auto& [pkg, components]: pkgIt->second) {
                        ProjectBuilder.FindPackage(pkg, components);
                    }
                }

                const auto libIt = InducedLinkLibs.find(dep.To().Id());
                if (libIt != InducedLinkLibs.end()) {
                    ProjectBuilder.AddTargetAttrs("target_link_libraries", libIt->second);
                }
                const auto allocIt = InducedAllocator.find(dep.To().Id());
                if (allocIt != InducedAllocator.end()) {
                    if (ProjectBuilder.SupportsAllocator()) {
                        ProjectBuilder.AddTargetMacro("target_allocator", allocIt->second, EMacroMergePolicy::ConcatArgs);
                    } else {
                        const auto* curTarget = ProjectBuilder.CurrentTarget();
                        const auto& cmakeTarget = *dynamic_cast<const TCMakeTarget*>(curTarget);

                        ProjectBuilder.AddTargetAttr(
                            "target_link_libraries",
                            curTarget && cmakeTarget.InterfaceTarget ? ECMakeAttrScope::Interface : ECMakeAttrScope::Public,
                            allocIt->second
                        );
                    }
                }
            }
            TBase::Left(state);
        }

        TProjectPtr TakeFinalizedProject() {
            return ProjectBuilder.FinishProject();
        }

        bool HasErrors() const noexcept {return FoundErrors;}

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
                std::string(toolName)
            };
            ProjectBuilder.AddDirMacro(
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

                for (const auto& tool: dep.To().Edges()) {
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
            ProjectBuilder.SetCurrentTools(std::move(tools));
        }

        bool CheckArgs(std::string_view sem, std::span<const std::string> args, TArgsConstraint constraint) {
            bool passed = true;
            std::string_view requirement;
            switch (constraint.Type) {
                case EConstraintType::Exact:
                    passed = args.size() == constraint.Count;
                    requirement = "exactly"sv;
                break;

                case EConstraintType::AtLeast:
                    passed = args.size() >= constraint.Count;
                    requirement = "at least"sv;
                break;

                case EConstraintType::MoreThan:
                    passed = args.size() > constraint.Count;
                    requirement = "more than"sv;
                break;
            }
            if (!passed) {
                const auto* subdir = ProjectBuilder.CurrentSubdir();
                const auto* tgt = ProjectBuilder.CurrentTarget();
                spdlog::error(
                    "{}@{}: semantic {} requires {} {} arguments. Provided arguments count {}.\n\tProvided arguments are: {}",
                    subdir ? subdir->Path.c_str() : "CMakeLists.txt",
                    tgt ? tgt->Name : "GLOBAL_SCOPE",
                    sem,
                    requirement,
                    constraint.Count,
                    args.size(),
                    fmt::join(args, ", ")
                );
                FoundErrors = true;
                return false;
            }
            return true;
        }

    private:
        THashMap<TNodeId, TInducedCmakePackages> InducedCmakePackages;
        THashMap<TNodeId, TAttrValues> InducedLinkLibs;
        THashMap<TNodeId, TVector<std::string>> InducedAllocator;

        THashMap<TStringBuf, const TProjectTarget*> TargetsDict;
        THashMap<TNodeId, const TProjectTarget*> Mod2Target;

        THashSet<TStringBuf> KnownTargets = {
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
        TCMakeProject::TBuilder ProjectBuilder;
        bool FoundErrors = false;
    };

}

bool RenderCmake(const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
{
    TCmakeRenderingVisitor visitor(projectConf, platform, globalProperties, cmakeGenerator);
    IterateAll(*platform.Graph, platform.StartDirs, visitor);
    auto project = visitor.TakeFinalizedProject();
    project.As<TCMakeProject>()->Save();
    return !visitor.HasErrors();
}

THashMap<fs::path, TSet<fs::path>> GetSubdirsTable(const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
{
    TCmakeRenderingVisitor visitor(projectConf, platform, globalProperties, cmakeGenerator);
    IterateAll(*platform.Graph, platform.StartDirs, visitor);
    auto project = visitor.TakeFinalizedProject();
    return project.As<TCMakeProject>()->GetSubdirsTable();
}

}
