#include "render_cmake.h"
#include "cmake_generator.h"
#include "fs_helpers.h"
#include "builder.h"

#include <devtools/yexport/known_modules.h_serialized.h>

#include <devtools/ymake/compact_graph/query.h>

#include <library/cpp/resource/resource.h>

#include <util/generic/set.h>
#include <util/generic/map.h>
#include <util/string/type.h>
#include <util/system/fs.h>

#include <spdlog/spdlog.h>

#include <span>
#include <type_traits>

namespace fs = std::filesystem;
using namespace std::literals;
using namespace NKnownModules;

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

    const THashMap<EKnownModules, TVector<std::string_view>> ModScripts = {
        {EKnownModules::YandexCommon, {"export_script_gen.py", "split_unittest.py", "generate_vcs_info.py"}},
        {EKnownModules::Swig, {"gather_swig_java.cmake"}},
        {EKnownModules::RecursiveLibrary, {"create_recursive_library_for_cmake.py"}}
    };

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

    struct TCMakeTarget {
        std::string Macro;
        std::string Name;
        TVector<std::string> MacroArgs;
        TMap<std::string, TAttrValues> Attributes;
        TMap<std::string, TVector<std::string>> Properties;
        TMultiMap<std::string, TVector<std::string>> Macros;
        TVector<std::pair<std::string, TVector<std::string>>> DirMacros;

        // target names for tools, used if cross-compilation is not enabled
        TSet<std::string> HostToolExecutableTargetDependencies;
        bool InterfaceTarget = false;
    };

    using TInducedCmakePackages = TMap<std::string, TSet<std::string>>;

    struct TCMakeList {
        TInducedCmakePackages Packages;
        TVector<std::string> Includes;
        TVector<std::pair<std::string, TVector<std::string>>> Macros;
        TVector<TCMakeTarget*> Targets;
        TSet<fs::path> Subdirectories;
        bool IsTopLevel = false;
    };

    using TSubdirsTable = THashMap<fs::path, TCMakeList>;
    using TSubdirsTableElem = THashMap<fs::path, TCMakeList>::value_type;

    class TCMakeProject {
    public:
        class TBuilder;

        TCMakeProject(const TProjectConf& projectConf, TPlatform& platform)
        : ProjectConf(projectConf)
        , Platform(platform)
        {
        }

        void Save() {
            // const auto dest
            THashSet<fs::path> platformDirs;
            TFile out = OpenOutputFile(ProjectConf.ExportRoot/Platform.Conf.CMakeListsFile);
            fmt::memory_buffer buf;
            auto bufIt = std::back_inserter(buf);

            fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);

            for (const auto* subdir: SubdirsOrder) {
                Y_ASSERT(subdir);
                Platform.SubDirs.insert(ProjectConf.ExportRoot/subdir->first);
                if (subdir->second.IsTopLevel) {
                    fmt::format_to(bufIt, "add_subdirectory({})\n", subdir->first.c_str());
                }
                SaveSubdirCMake(subdir->first, subdir->second);
            }

            out.Write(buf.data(), buf.size());
            spdlog::info("Root {} saved", Platform.Conf.CMakeListsFile);
        }

        THashMap<fs::path, TSet<fs::path>> GetSubdirsTable() const {
            THashMap<fs::path, TSet<fs::path>> result;
            for (const auto* subdir: SubdirsOrder) {
                result.insert({subdir->first, subdir->second.Subdirectories});
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
            TFile out = OpenOutputFile(ProjectConf.ExportRoot/subdir/Platform.Conf.CMakeListsFile);
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
            for (const auto& addSubdir: data.Subdirectories) {
                fmt::format_to(bufIt, "add_subdirectory({})\n", addSubdir.c_str());
            }

            PrintDirLevelMacros(bufIt, data.Macros);

            for (const auto& tgt: data.Targets) {
                if (buf.size() != 0) {
                    fmt::format_to(bufIt, "\n");
                }
                // Target definition
                if (tgt->InterfaceTarget) {
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
                for (const auto& [name, values]: tgt->Properties) {
                    if (values.empty()) {
                        continue;
                    }
                    fmt::format_to(bufIt, "set_property(TARGET {} PROPERTY\n  {} {}\n)\n", tgt->Name, name, fmt::join(values, " "));
                }
                // Target attributes
                for (const auto& [macro, values]: tgt->Attributes) {
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
                PrintDirLevelMacros(bufIt, tgt->DirMacros);

                // Target level macros
                for (const auto& [macro, values]: tgt->Macros) {
                    if (values.empty()) {
                        fmt::format_to(bufIt, "{}({})\n", macro, tgt->Name);
                    } else {
                        fmt::format_to(bufIt, "{}({}\n  {}\n)\n", macro, tgt->Name, fmt::join(values, "\n  "));
                    }
                }

                // Dependencies
                if (!tgt->HostToolExecutableTargetDependencies.empty()) {
                    fmt::format_to(
                        bufIt,
                        "if(NOT CMAKE_CROSSCOMPILING)\n  add_dependencies({}\n    {}\n)\nendif()\n",
                        tgt->Name,
                        fmt::join(tgt->HostToolExecutableTargetDependencies, "\n    ")
                    );
                }
            }

            auto writeToFile = [&out](const fs::path& path) {
                TFileInput fileInput(TFile{path.string(), RdOnly});
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

            spdlog::info("{}/{} saved", subdir.string(), Platform.Conf.CMakeListsFile);
        }

    private:
        static constexpr std::string_view CMakeEpilogueFile = "epilogue.cmake";
        static constexpr std::string_view CMakePrologueFile = "prologue.cmake";

    private:
        TDeque<TCMakeTarget> Targets;
        TSubdirsTable Subdirs;
        TVector<TSubdirsTableElem*> SubdirsOrder;
        const TProjectConf& ProjectConf;
        TPlatform& Platform;
    };

    class TCMakeProject::TBuilder: public TGeneratorBuilder<TSubdirsTableElem, TCMakeTarget> {
    public:

        TBuilder(const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
        : Project(projectConf, platform)
        , Platform(platform)
        , GlobalProperties(globalProperties)
        , CMakeGenerator(cmakeGenerator)
        {}

        TCMakeProject Finalize() { return std::move(Project);}

        void SetCurrentTools(TMap<std::string_view, std::string_view> tools) noexcept {
            Tools = std::move(tools);
        }

        TTargetHolder CreateTarget(const std::string& targetMacro, const fs::path& targetDir, const std::string name, std::span<const std::string> macroArgs);

        void AddSubdirectories(const fs::path& targetDir);

        void AddTargetAttr(const std::string& attrMacro, ECMakeAttrScope scope, std::span<const std::string> vals) {
            if (!CurTarget) {
                spdlog::info("attempt to add target attribute '{}' while there is no active target", attrMacro);
                return;
            }
            if (vals.empty()) {
                return;
            }
            if (CurTarget->InterfaceTarget && attrMacro == "target_sources") {
                scope = ECMakeAttrScope::Interface;
            }

            TVector<std::string>* dest;
            switch(scope) {
            case ECMakeAttrScope::Interface:
                dest = &CurTarget->Attributes[attrMacro].Iface;
            break;
            case ECMakeAttrScope::Public:
                dest = CurTarget->InterfaceTarget ? &CurTarget->Attributes[attrMacro].Iface : &CurTarget->Attributes[attrMacro].Pub;
            break;
            case ECMakeAttrScope::Private:
                dest = CurTarget->InterfaceTarget ? nullptr : &CurTarget->Attributes[attrMacro].Priv;
            break;
            }

            if (dest) {
                for (const auto& val: vals) {
                    dest->push_back(ConvertArg(val));
                }
            }
        }

        void SetTargetMacroArgs(TVector<std::string> args) {
            if (!CurTarget) {
                spdlog::error("attempt to set target macro args while there is no active target");
                return;
            }
            CurTarget->MacroArgs = std::move(args);
        }

        void AddTargetAttrs(const std::string& attrMacro, const TAttrValues& values) {
            if (!CurTarget) {
                spdlog::error("attempt to add target attribute '{}' while there is no active target", attrMacro);
                return;
            }
            auto& destAttrs = CurTarget->Attributes[attrMacro];
            Copy(values.Iface.begin(), values.Iface.end(), std::back_inserter(destAttrs.Iface));
            Copy(values.Pub.begin(), values.Pub.end(), std::back_inserter(CurTarget->InterfaceTarget ? destAttrs.Iface : destAttrs.Pub));
            if (CurTarget->InterfaceTarget) {
                Copy(values.Priv.begin(), values.Priv.end(), std::back_inserter(destAttrs.Priv));
            }
        }

        void AddDirMacro(const std::string& name, std::span<const std::string> args) {
            if (!CurList) {
                spdlog::error("attempt to add macro '{}' while there is no active CMakeLists.txt", name);
                return;
            }
            CurList->second.Macros.push_back({name, ConvertMacroArgs(args)});
        }

        void AddTargetMacro(const std::string& name, std::span<const std::string> args, EMacroMergePolicy mergePolicy) {
            if (!CurTarget) {
                spdlog::error("attempt to add target attribute '{}' while there is no active target", name);
                return;
            }
            switch (mergePolicy) {
                case EMacroMergePolicy::ConcatArgs: {
                    auto pos = CurTarget->Macros.find(name);
                    if (pos == CurTarget->Macros.end())
                        pos = CurTarget->Macros.insert({name, {}});
                    Copy(args.begin(), args.end(), std::back_inserter(pos->second));
                    break;
                }
                case EMacroMergePolicy::FirstCall:
                    if (CurTarget->Macros.contains(name))
                        break;
                    [[fallthrough]];
                case EMacroMergePolicy::MultipleCalls:
                    CurTarget->Macros.emplace(name, ConvertMacroArgs(args)); break;
            }
        }

        // at directory level but should logically be grouped with the current target
        void AddTargetDirMacro(const std::string& name, std::span<const std::string> args) {
            if (!CurTarget) {
                spdlog::error("attempt to add target directory macro '{}' while there is no active target", name);
                return;
            }
            CurTarget->DirMacros.push_back({name, ConvertMacroArgs(args)});
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
            GlobalProperties.GlobalModules.emplace("conan.cmake", TGlobalCMakeModuleFlags{true});
        }

        void RequireConanToolPackage(const std::string& name) {
            GlobalProperties.ConanToolPackages.insert(name);
            GlobalProperties.GlobalModules.emplace("conan.cmake", TGlobalCMakeModuleFlags{true});
        }

        void ImportConanArtefact(const std::string& importSpec) {
            GlobalProperties.ConanImports.insert(importSpec);
        }

        void AddConanOption(const std::string& optSpec) {
            GlobalProperties.ConanOptions.insert(optSpec);
        }

        void AppendTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget->Properties[name];
            for (const auto& val: values) {
                prop.push_back(val);
            }
        }

        void SetTargetProperty(const std::string& name, std::span<const std::string> values) {
            auto& prop = CurTarget->Properties[name];
            prop.clear();
            for (const auto& val: values) {
                prop.push_back(val);
            }
        }

        void AddArcadiaScript(const fs::path& path) {
            GlobalProperties.ArcadiaScripts.insert(path.lexically_relative(ArcadiaScriptsRoot));
        }

        void UseGlobalFindModule(const std::string& pkg) {
            GlobalProperties.GlobalModules.emplace(fmt::format("Find{}.cmake", pkg), TGlobalCMakeModuleFlags{false});
        }

        void UseGlobalModule(EKnownModules mod) {
            GlobalProperties.GlobalModules.emplace(ToString(mod), TGlobalCMakeModuleFlags{true});

            const auto scripts = ModScripts.find(mod);
            if (scripts == ModScripts.end()) {
                return;
            }
            for (std::string_view script: scripts->second) {
                GlobalProperties.ExtraScripts.insert(std::string{script});
            }
        }

        void FindPackage(const std::string& name, TSet<std::string> components) {
            if (!CurList) {
                spdlog::error("attempt to add find_package macro while there is no active CMakeLists.txt");
                return;
            }
            CurList->second.Packages[name].insert(components.begin(), components.end());
        }

        void Include(const std::string& mod) {
            if (!CurList) {
                spdlog::error("attempt to add find_package macro while there is no active CMakeLists.txt");
                return;
            }
            CurList->second.Includes.push_back(mod);
        }

        void SetFakeFlag(bool isFake) {
            if (CurTarget) {
                CurTarget->InterfaceTarget = isFake;
            }
        }

        void AddSemantics(const std::string& semantica) {
            CMakeGenerator->OnAttribute(semantica);
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
                            CurTarget->HostToolExecutableTargetDependencies.insert(TString(toolName));
                            return fmt::format("${{{}}}", GetToolBinVariable(toolName));
                        }
                    }
                }
            }
            return arg;
        }

    private:
        TCMakeProject Project;
        TPlatform& Platform;
        TGlobalProperties& GlobalProperties;
        TCMakeGenerator* CMakeGenerator;

        // target_name -> path_in_arcadia
        TMap<std::string_view, std::string_view> Tools;
    };

    TCMakeProject::TBuilder::TTargetHolder TCMakeProject::TBuilder::CreateTarget(const std::string& targetMacro, const fs::path& targetDir, const std::string name, std::span<const std::string> macroArgs) {
        const auto [pos, inserted] = Project.Subdirs.insert({targetDir, {}});
        if (inserted) {
            Project.SubdirsOrder.push_back(&*pos);
            AddSubdirectories(targetDir);
        }
        Project.Targets.push_back({.Macro = targetMacro, .Name = name, .MacroArgs = {macroArgs.begin(), macroArgs.end()}});
        pos->second.Targets.push_back(&Project.Targets.back());
        return {*this, *pos, Project.Targets.back()};
    }

    void TCMakeProject::TBuilder::AddSubdirectories(const fs::path& targetDir) {
        auto dir = targetDir;
        while(dir.has_parent_path()) {
            const auto [pos, inserted] = Project.Subdirs.insert({dir.parent_path(), {}});
            if (inserted) {
                Project.SubdirsOrder.push_back(&*pos);
            }
            pos->second.Subdirectories.insert(dir.filename());
            dir = dir.parent_path();
        }
        Project.Subdirs.at(dir).IsTopLevel = true;
    }

    struct TExtraStackData {
        TCMakeProject::TBuilder::TTargetHolder CurTargetHolder;
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

                if (const auto it = MacroToModuleMap.find(semName); it != MacroToModuleMap.end()) {
                    ProjectBuilder.UseGlobalModule(it->second);
                }
                if (semName == "IGNORED") {
                    traverseFurther = false;
                } else if (KnownTargets.contains(semName)) {
                    if (!CheckArgs(semName, semArgs, AtLeast(2)))
                        continue;

                    state.Top().CurTargetHolder = ProjectBuilder.CreateTarget(semName, semArgs[0].c_str(), semArgs[1], semArgs.subspan(2));
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
                        spdlog::error("No proto plugin tool found '{}' for target '{}'", semArgs[1], ProjectBuilder.CurrentTarget()->Name);
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
                    const auto linkLibs = fres->second->Attributes.find("target_link_libraries");
                    if (linkLibs != fres->second->Attributes.end()) {
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
                    if (supportsAllocator(*ProjectBuilder.CurrentTarget())) {
                        ProjectBuilder.AddTargetMacro("target_allocator", allocIt->second, EMacroMergePolicy::ConcatArgs);
                    } else {
                        ProjectBuilder.AddTargetAttr(
                            "target_link_libraries",
                            ProjectBuilder.CurrentTarget()->InterfaceTarget ? ECMakeAttrScope::Interface : ECMakeAttrScope::Public,
                            allocIt->second
                        );
                    }
                }
            }
            TBase::Left(state);
        }

        TCMakeProject TakeFinalizedProject() {
            return ProjectBuilder.Finalize();
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

        bool supportsAllocator(const TCMakeTarget& tgt) const {
            // Ugly hack here. In Jinja generator world PROGRAM and DLL target should use their own templates
            // and handle allocators in a different way then other target types
            return tgt.Macro == "add_executable"sv || (tgt.Macro == "add_library" && !tgt.MacroArgs.empty() && tgt.MacroArgs[0] == "SHARED"sv);
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
                const auto* list = ProjectBuilder.CurrentList();
                const auto* tgt = ProjectBuilder.CurrentTarget();
                spdlog::error(
                    "{}@{}: semantic {} requires {} {} arguments. Provided arguments count {}.\n\tProvided arguments are: {}",
                    list ? list->first.c_str() : "CMakeLists.txt",
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

        THashMap<TStringBuf, const TCMakeTarget*> TargetsDict;
        THashMap<TNodeId, const TCMakeTarget*> Mod2Target;

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
        THashMap<TStringBuf, EKnownModules> MacroToModuleMap = {
            { "add_global_library_for", EKnownModules::YandexCommon },
            { "add_fat_object", EKnownModules::FatObject },
            { "add_shared_library", EKnownModules::SharedLibs },
            { "add_recursive_library", EKnownModules::RecursiveLibrary },
            { "archive", EKnownModules::YandexCommon },
            { "conan_add_remote", EKnownModules::Conan },
            { "conan_check", EKnownModules::Conan },
            { "conan_cmake_autodetect", EKnownModules::Conan },
            { "conan_cmake_configure", EKnownModules::Conan },
            { "conan_cmake_detect_unix_libcxx", EKnownModules::Conan },
            { "conan_cmake_detect_vs_runtime", EKnownModules::Conan },
            { "conan_cmake_generate_conanfile", EKnownModules::Conan },
            { "conan_cmake_install", EKnownModules::Conan },
            { "conan_cmake_run", EKnownModules::Conan },
            { "conan_cmake_settings", EKnownModules::Conan },
            { "conan_cmake_setup_conanfile", EKnownModules::Conan },
            { "conan_config_install", EKnownModules::Conan },
            { "conan_load_buildinfo", EKnownModules::Conan },
            { "conan_parse_arguments", EKnownModules::Conan },
            { "copy_file", EKnownModules::YandexCommon },
            { "curdir_masm_flags", EKnownModules::Masm },
            { "generate_enum_serilization", EKnownModules::YandexCommon },
            { "llvm_compile_c", EKnownModules::LlvmTools },
            { "llvm_compile_cxx", EKnownModules::LlvmTools },
            { "old_conan_cmake_install", EKnownModules::Conan },
            { "resources", EKnownModules::YandexCommon },
            { "run_antlr", EKnownModules::Antlr },
            { "target_joined_source", EKnownModules::YandexCommon },
            { "target_ev_messages", EKnownModules::Protobuf },
            { "target_proto_messages", EKnownModules::Protobuf },
            { "target_proto_plugin", EKnownModules::Protobuf },
            { "target_ragel_lexers", EKnownModules::YandexCommon },
            { "target_yasm_source", EKnownModules::YandexCommon },
            { "vcs_info", EKnownModules::YandexCommon },
            { "target_bison_parser", EKnownModules::Bison },
            { "target_flex_lexers", EKnownModules::Bison},
            { "target_fbs_source", EKnownModules::Fbs },
            { "target_cuda_flags", EKnownModules::Cuda },
            { "target_cuda_cflags", EKnownModules::Cuda },
            { "target_rodata_sources", EKnownModules::Archive },
            { "target_proto_outs", EKnownModules::Protobuf },
            { "target_proto_addincls", EKnownModules::Protobuf },
            { "target_sources_custom", EKnownModules::YandexCommon },
            { "use_export_script", EKnownModules::YandexCommon },
            { "target_cuda_sources", EKnownModules::Cuda },
            { "target_cython_sources", EKnownModules::Cython },
            { "target_cython_options", EKnownModules::Cython },
            { "target_cython_include_directories", EKnownModules::Cython },
            { "set_python_type_for_cython", EKnownModules::Cython },
            { "swig_add_library", EKnownModules::Swig },
            { "add_jar", EKnownModules::Swig },
            { "add_yunittest", EKnownModules::YandexCommon },
            { "set_yunittest_property", EKnownModules::YandexCommon }
        };
        THashSet<TString> BundledFindModules = {"AIO", "IDN", "JNITarget"};
        TCMakeProject::TBuilder ProjectBuilder;
        bool FoundErrors = false;
    };

}

bool RenderCmake(const TVector<TNodeId>& startDirs, const TSemGraph& graph, const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
{
    TCmakeRenderingVisitor visitor(projectConf, platform, globalProperties, cmakeGenerator);
    IterateAll(graph, startDirs, visitor);
    visitor.TakeFinalizedProject().Save();
    return !visitor.HasErrors();
}

THashMap<fs::path, TSet<fs::path>> GetSubdirsTable(const TVector<TNodeId>& startDirs, const TSemGraph& graph, const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator)
{
    TCmakeRenderingVisitor visitor(projectConf, platform, globalProperties, cmakeGenerator);
    IterateAll(graph, startDirs, visitor);
    return visitor.TakeFinalizedProject().GetSubdirsTable();
}
