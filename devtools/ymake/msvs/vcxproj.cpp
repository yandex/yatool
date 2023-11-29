#include "vcxproj.h"

#include "command.h"
#include "configuration.h"
#include "error.h"
#include "file.h"
#include "guid.h"
#include "misc.h"
#include "project.h"
#include "version.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/iter.h>

#include <devtools/draft/cmd.h>
#include <devtools/draft/file.h>

#include <library/cpp/resource/resource.h>

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/is_in.h>
#include <util/generic/string.h>
#include <util/stream/file.h>
#include <util/stream/str.h>
#include <library/cpp/deprecated/split/split_iterator.h>
#include <util/string/subst.h>
#include <util/system/fs.h>

#include <functional>

namespace NYMake {
    namespace NMsvs {
        namespace {

            const TStringBuf PROJECTS_DIR = "Projects";
            const TStringBuf INTERMEDIATE_DIR = "MSVS";
            const TStringBuf ALL_PROJECT_PREFIX = "all_";

            const TStringBuf MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003";
            const TStringBuf TOOLS_VERSION = "4.0";

            const TStringBuf OBJ_EXT = "obj";

            const size_t MAX_SOURCE_NAME_DUPS = 256;

            enum EComType {
                Link,
                Compile,
                CompileAsm,
                Custom,
            };

            EComType GetCommandType(const TFile& outFile, const TCommand& cmd) {
                //if (outFile.IsTarget())
                //    return Link;
                if (outFile.IsObj()) {
                    if (IsAsmSourcePath(cmd.SourceInput())) {
                        return CompileAsm;
                    }
                    if (IsCuSourcePath(cmd.SourceInput())) {
                        return Custom;
                    }
                    return Compile;
                }
                return Custom;
            }

            inline TString GenCustomBuildString(TModule& mod, const TCommands& cmds, EConf conf) {
                const char* err = "if %errorlevel% neq 0 goto :cmEnd\n";
                const char* cmdFin = ":cmEnd\n"
                                     "endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone\n"
                                     ":cmErrorLevel\n"
                                     "exit /b %1\n"
                                     ":cmDone\n"
                                     "if %errorlevel% neq 0 goto :VCEnd";
                TString res = "setlocal\n";
                for (size_t i = 0; i < cmds.size(); ++i) {
                    TCommand cmd = cmds[i];
                    TString text = cmd.CommandText(conf);
                    SubstGlobal(text, "&&", "\n"); // Command list likes newlines
                    NDev::TOsCommandList commands;
                    NDev::GetCommandList(commands, text);

                    const TString dir = WindowsPath(mod.Bindir());
                    res += "cd /d " + dir + "\n";
                    res += err;

                    for (const auto& curCmd : commands) {
                        // 1. If a program name does not contain / - it has not to be quoted
                        //    (for example - "cd" != cd in Windows CMD)
                        const auto& programName = curCmd.Args.front();
                        if (programName.Contains('/') || programName.Contains('\\')) {
                            res += '"';
                            res += programName;
                            res += '"';
                            res += ' ';
                        } else {
                            res += programName;
                            res += ' ';
                            // Crutch for crutches
                            if (programName == "cd") {
                                res += "/d ";
                            }
                        }

                        // 2. All args have to be quoted (except /Dfoo="bar")
                        // 3. All quotes in xml doc have to be escaped, 'cause in other case
                        //    escaped symbols inside the quotes will not be unescaped in command prompt
                        bool first = true;
                        for (auto arg_it = begin(curCmd.Args) + 1; arg_it != end(curCmd.Args); ++arg_it) {
                            const auto& arg = * arg_it;

                            if (!first) {
                                res += ' ';
                            } else {
                                first = false;
                            }

                            // Another crutch :(
                            if (arg.StartsWith("/D") || arg.StartsWith("-D")) {
                                res += arg;
                            } else {
                                res += '"';
                                res += arg;
                                res += '"';
                            }
                        }

                        if (curCmd.Stdout.size())
                            res += "> " + curCmd.Stdout;

                        res += '\n';
                        res += err;
                    }
                }
                res += cmdFin;
                return res;
            }

            inline void SetNodeCondition(NXml::TNode node, EConf conf) {
                TStringStream condition;
                condition << "'$(Configuration)|$(Platform)'=='" << Configuration(conf) << "|x64'";
                node.SetAttr("Condition", condition.Str());
            }

            // TODO: refactor and systemize
            struct TKnownFlags {
                THashMap<TString, TString> Properties;
                size_t Version;

                explicit TKnownFlags(size_t version)
                    : Properties()
                    , Version(version)
                {
                }

                TString Slice(const TString& orig, const std::function<bool(const TStringBuf&, const TStringBuf&)>& parseParam) {
                    TString ret;
                    ret.reserve(orig.size());
                    const TSplitDelimiters delims(" ");
                    const TSplitDelimiters screens("\"");
                    const TScreenedDelimitersSplit splitter(orig, delims, screens);
                    TScreenedDelimitersSplit::TIterator it = splitter.Iterator();
                    while (!it.Eof()) {
                        TStringBuf param = it.NextTok();
                        TStringBuf value = param;
                        TStringBuf name = value.NextTok(':');
                        if (parseParam(name, value)) {
                            continue;
                        }
                        if (ret.size()) {
                            ret += ' ';
                        }
                        ret += param;
                    }
                    return ret;
                }
            };

            struct TKnownCFlags : TKnownFlags {
                explicit TKnownCFlags(size_t version)
                    : TKnownFlags(version)
                {
                    Properties["WarningLevel"] = "Level4";
                    Properties["RemoveUnreferencedCodeData"] = "";
                }

                TString Slice(const TString& orig) {
                    return TKnownFlags::Slice(orig, [this](const TStringBuf& name, const TStringBuf& value) {
        Y_UNUSED(value);
        if (name == "/w") {
            Properties["WarningLevel"] = "TurnOffAllWarnings";
            return true;
        }
        return false;
                    });
                }
            };

            struct TKnownLinkFlags : TKnownFlags {
                explicit TKnownLinkFlags(size_t version)
                    : TKnownFlags(version)
                {
                    Properties["GenerateDebugInformation"] = (Version < 140) ? "true" : "Debug";
                    Properties["SubSystem"] = "Console";
                }

                TString Slice(const TString& orig) {
                    return TKnownFlags::Slice(orig, [this](const TStringBuf& name, const TStringBuf& value) {
        if (name == "/DYNAMICBASE") {
            Properties["RandomizedBaseAddress"] = (value == "NO") ? "false" : "true";
            return true;
        }
        if (name == "/DEBUG") {
            if (Version < 140) {
                Properties["GenerateDebugInformation"] = "true";
            }
            Properties["GenerateDebugInformation"] = (value == "FASTLINK") ? "DebugFastLink" : "Debug";
            return true;
        }
        return false;
                    });
                }
            };

            inline void AddProperties(NXml::TNode& node, const TKnownFlags& flags, EConf conf = C_UNSET) {
                for (const auto& flag : flags.Properties) {
                    NXml::TNode newNode = node.AddChild(flag.first, flag.second);
                    if (conf != C_UNSET) {
                        SetNodeCondition(newNode, conf);
                    }
                }
            }

            struct TVcxprojRenderer {
                NXml::TDocument Document;
                NXml::TNamespacesForXPath Namespaces;
                NXml::TNode Root;

                explicit TVcxprojRenderer(const TStringBuf& tmpl)
                    : Document(NResource::Find(tmpl), NXml::TDocument::String)
                    , Namespaces{NXml::TNamespaceForXPath{"ms", TString{MSBUILD_NS}}}
                    , Root(Document.Root())
                {
                    Root.SetAttr("DefaultTargets", "Build");
                    Root.SetAttr("ToolsVersion", TOOLS_VERSION);
                }

                inline void AddProjectConfigurations() {
                    //Adding project configurations
                    NXml::TNode node = Root.Node("ms:ItemGroup[@Label='ProjectConfigurations']", false, Namespaces);
                    node = node.AddChild("ProjectConfiguration");
                    node.SetAttr("Include", "Debug|x64");
                    node.AddChild("Configuration", "Debug");
                    node.AddChild("Platform", "x64");
                    node = node.Node("..");
                    node = node.AddChild("ProjectConfiguration");
                    node.SetAttr("Include", "Release|x64");
                    node.AddChild("Configuration", "Release");
                    node.AddChild("Platform", "x64");
                }

                inline void AddGlobals(const TStringBuf& name, const TStringBuf& guid, const size_t& Version) {
                    //Adding project globals
                    NXml::TNode node = Root.Node("ms:PropertyGroup[@Label='Globals']", false, Namespaces);
                    node.AddChild("ProjectGUID", TString("{") + guid + "}");
                    node.AddChild("Keyword", "Win32Proj");
                    node.AddChild("Platform", "x64");
                    node.AddChild("ProjectName", name);
                    //Get latest windows sdk 10 for msvs 2017
                    if (Version >= 141) {
                        node.AddChild("LatestTargetPlatformVersion", "$([Microsoft.Build.Utilities.ToolLocationHelper]::GetLatestSDKTargetPlatformVersion('Windows', '10.0'))");
                        node.AddChild("WindowsTargetPlatformVersion", "$(LatestTargetPlatformVersion)");
                    }
                }

                inline void AddConfiguration(const TStringBuf& configurationType, const TStringBuf& platformToolSet, const TStringBuf& toolsVersion) {
                    //Adding configs
                    NXml::TNode node = Root.Node("ms:PropertyGroup[@Label='Configuration']", false, Namespaces);
                    node.AddChild("ConfigurationType", configurationType);
                    node.AddChild("UseOfMfc", "false");
                    node.AddChild("CharacterSet", "Multibyte");
                    node.AddChild("PlatformToolset", platformToolSet);
                    if (!toolsVersion.empty()) {
                        node.AddChild("VCToolsVersion", toolsVersion);
                    }
                }

                inline void AddArcadiaProps() {
                    // Adding Arcadia.cpp.props file
                    NXml::TNode node = Root.Node("ms:ImportGroup[@Label='ArcadiaProps']", false, Namespaces);
                    node = node.AddChild("Import");
                    node.SetAttr("Project", "$(SolutionDir)\\Arcadia.Cpp.props");
                }

                inline bool Finish(const TString& filename) {
                    TString data = Document.ToString("UTF-8");
                    SubstGlobal(data, "\n", "\r\n");
                    NDev::TModifiedFile file(filename);
                    file << data;
                    file.Finish();
                    return file.WasModified();
                }
            };

        }

        bool TFlatObjPool::AddSource(const TStringBuf& sourcePath, TString& objName) {
            TString sourceName(sourcePath);
            ::SubstGlobal(sourceName, '\\', '/');
            sourceName = NPath::Basename(sourceName);
            if (!IsRegularSourcePath(sourceName)) {
                YWarn() << "Source file doesn't look like source: " << sourcePath << Endl;
            }
            TString name(NPath::NoExtension(sourceName));
            auto inserted = Set.insert(name);
            if (inserted.second) {
                objName = *inserted.first + "." + OBJ_EXT;
                return true;
            }
            name += "_extra";
            for (size_t index = 0; index < MAX_SOURCE_NAME_DUPS; ++index) {
                inserted = Set.insert(name + ::ToString(index + 1));
                if (inserted.second) {
                    objName = *inserted.first + "." + OBJ_EXT;
                    return false;
                }
            }
            ythrow TMsvsError() << "Too many source files with the same name: " << sourceName;
        }

        TVcRender::TVcRender(TYMake& yMake, const TFsPath& solutionRoot, const TStringBuf& name, size_t version)
            : TBase{TDependencyFilter{TDependencyFilter::SkipRecurses}}
            , YMake(yMake)
            , Name(name)
            , Version(ToolSetVersion(version))
            , SolutionRoot(solutionRoot)
            , IntermediateDir()
            , AllProject(TString{ALL_PROJECT_PREFIX} + Name)
            , UseArcadiaToolchain(::IsTrue(YMake.Conf.CommandConf.EvalValue("USE_ARCADIA_TOOLCHAIN")))
        {
        }

        // TODO: merge vcxproj and allvcxproj
        void TVcRender::RenderVcxproj(TModule& module, const TSimpleSharedPtr<TUniqVector<TNodeId>>& peers, const TSimpleSharedPtr<TUniqVector<TNodeId>>& tools) {
            TProject project(module);
            YConfInfo(Sln) << "rendering vcxproj for " << module.Name << Endl;

            // TODO: unify peers retrieval
            ModSlnInfo.emplace(module, module.RetrievePeers());

            const TProjectTree::TNode& projectNode = ProjectTree.Add(module);
            const TStringBuf& title = projectNode.Name;
            const TStringBuf& path = projectNode.Path;
            TString guid = projectNode.Guid();

            TVcxprojRenderer renderer("proj.xml");

            //NXml::TDocument vcFile(NResource::Find("proj.xml"), NXml::TDocument::String);
            //NXml::TNamespaceForXPath ns = {"ms", MSBUILD_NS.ToString()};
            //NXml::TNamespacesForXPath nss;
            //nss.push_back(ns);

            renderer.AddProjectConfigurations();
            renderer.AddGlobals(title, guid, Version);
            TString configType = TString::Join(module.IsFakeModule() ? "Empty" : "", project.ConfigurationType());
            renderer.AddConfiguration(configType, GetPlatformToolset(), GetToolsVersion());

            if (UseArcadiaToolchain) {
                renderer.AddArcadiaProps();
            }

            TString moduleIntDir = NPath::Join(IntermediateDir, guid);

            //Adding dir/name properties
            NXml::TNode curNode = renderer.Root.Node("//ms:PropertyGroup[not(@*)]", false, renderer.Namespaces);
            SetNodeCondition(curNode.AddChild("LinkIncremental", "true"), C_DEBUG);
            SetNodeCondition(curNode.AddChild("LinkIncremental", "false"), C_RELEASE);
            curNode.AddChild("TargetName", module.Name.BasenameWithoutExtension());
            curNode.AddChild("TargetExtention", project.TargetExtention());
            curNode.AddChild("OutDir", WindowsPath(module.Bindir()) + "\\");
            curNode.AddChild("IntDir", WindowsPath(moduleIntDir) + "\\");

            //Adding Definitions
            curNode = renderer.Root.Node("//ms:ItemDefinitionGroup[not(@*)]", false, renderer.Namespaces);
            curNode = curNode.Node("./ms:ClCompile", false, renderer.Namespaces);
            TKnownCFlags dbgFlags(Version), relFlags(Version);
            SetNodeCondition(curNode.AddChild("AdditionalOptions", dbgFlags.Slice(module.CFlags(C_DEBUG))), C_DEBUG);
            SetNodeCondition(curNode.AddChild("AdditionalOptions", relFlags.Slice(module.CFlags(C_RELEASE))), C_RELEASE);
            AddProperties(curNode, dbgFlags, C_DEBUG);
            AddProperties(curNode, relFlags, C_RELEASE);

            // Incdirs
            TString addIncDirs = module.IncludeString(";");
            curNode.AddChild("AdditionalIncludeDirectories", addIncDirs + ";%(AdditionalIncludeDirectories)");

            // Adding global lib
            if (module.IsGlobalModule()) {
                GlobalLibPool.Add(module, { NPath::Join(module.Bindir(), module.Name.Basename()) });
            }

            //Adding Link
            // TODO: check Link node for static libs
            curNode = renderer.Root.Node("//ms:Link", false, renderer.Namespaces);
            TStringStream linkInputs;
            for (const auto& lib : module.LinkStdLibs()) {
                linkInputs << lib << ';';
            }
            if (::IsIn({TModule::K_PROGRAM, TModule::K_DLL}, module.Kind())) {
                TModules peerReqs;
                if (peers) {
                    ToSortedData(peerReqs, *peers, YMake);
                }
                for (const auto& peer : peerReqs) {
                    for (const auto& globalObj : peer.GlobalObjs()) {
                        if (GlobalObjPool.Contains(globalObj)) {
                            linkInputs << WindowsPath(GlobalObjPool.Path(globalObj)) << ';';
                        } else if (GlobalLibPool.Contains(globalObj)) {
                            linkInputs << WindowsPathWithPrefix("/WHOLEARCHIVE:", GlobalLibPool.Path(globalObj)) << ';';
                        } else {
                            ythrow TMsvsError() << "File not found in global pools: " << globalObj.Name;
                        }
                    }
                }
            }
            linkInputs << "%(AdditionalDependencies)";
            curNode.AddChild("AdditionalDependencies", linkInputs.Str());

            TKnownLinkFlags dbgLinkFlags(Version), relLinkFlags(Version);
            SetNodeCondition(curNode.AddChild("AdditionalOptions", dbgLinkFlags.Slice(module.LinkFlags(C_DEBUG))), C_DEBUG);
            SetNodeCondition(curNode.AddChild("AdditionalOptions", relLinkFlags.Slice(module.LinkFlags(C_RELEASE))), C_RELEASE);
            AddProperties(curNode, dbgLinkFlags, C_DEBUG);
            AddProperties(curNode, relLinkFlags, C_RELEASE);

            //Adding commands
            curNode = renderer.Root.Node("//ms:ItemGroup[not(@*)]", false, renderer.Namespaces);
            TMap<TString, TCommands> customBuildCommands; // Win()
            THashSet<TString> files;
            TFlatObjPool flatObjPool;
            THashSet<TString> sourcesWithoutExt;
            for (const auto& file : module.Files) {
                if (!module.FileWithBuildCmd(file)) {
                    continue;
                }
                TCommand command(YMake, file.Id(), module.GetModuleId()); // TODO (OPTIMIZE): use some data is available in module.ModuleCmd
                switch (GetCommandType(file, command)) {
                    case Link:
                        continue;
                    case Compile: {
                        TString source = command.SourceInput();
                        curNode = curNode.AddChild("ClCompile");
                        auto winSource = WindowsPath(source);
                        auto inserted = files.insert(winSource);
                        Y_ASSERT(inserted.second);
                        curNode.SetAttr("Include", *inserted.first);
                        if (IsCSourcePath(source)) {
                            curNode.AddChild("CompileAs", "CompileAsC");
                        }
                        TString objName;
                        if (!flatObjPool.AddSource(source, objName)) {
                            objName.prepend("$(IntDir)");
                            curNode.AddChild("ObjectFileName", objName);
                        }
                        if (module.FileGlobal(file.Id())) {
                            GlobalObjPool.Add(file, {objName, moduleIntDir});
                        }
                        if (const auto fileOutSuffixIt = module.FileOutSuffix.find(file.Id())) {
                            TString newObjName = NPath::AnyBasename(source) + fileOutSuffixIt->second + "." + OBJ_EXT;
                            newObjName.prepend("$(IntDir)");
                            SetNodeCondition(curNode.AddChild("ObjectFileName", newObjName), C_DEBUG);
                            SetNodeCondition(curNode.AddChild("ObjectFileName", newObjName), C_RELEASE);
                        }
                        if (const auto fileFlagIt = module.FileFlags.find(file.Id())) {
                            TString options = fileFlagIt->second + " %(AdditionalOptions)";
                            SetNodeCondition(curNode.AddChild("AdditionalOptions", options), C_DEBUG);
                            SetNodeCondition(curNode.AddChild("AdditionalOptions", options), C_RELEASE);
                        }
                        curNode = curNode.Node("..");
                        // this need to filter headers
                        if (module.IsGlobalModule()) {
                            sourcesWithoutExt.insert(TString(NPath::NoExtension(winSource)));
                        }
                        break;
                    }
                    case CompileAsm: {
                        customBuildCommands[command.SourceInput()].push_back(command);
                        break;
                    }
                    case Custom: {
                        TString inp = command.Inputs().size() ? command.SourceInput() : command.Tools().size() ? command.Tools()[0].Name : "[_no_inputs_]"; // at least one is not empty
                        YDIAG(Sln) << "make code for " << inp << Endl;
                        customBuildCommands[inp].push_back(command);
                        break;
                    }
                }
            }
            for (const auto& cmd_i : customBuildCommands) {
                const TCommands& cmds = cmd_i.second;
                curNode = curNode.AddChild("CustomBuild");
                SetNodeCondition(curNode.AddChild("Command", GenCustomBuildString(module, cmds, C_DEBUG)), C_DEBUG);
                SetNodeCondition(curNode.AddChild("Command", GenCustomBuildString(module, cmds, C_RELEASE)), C_RELEASE);
                auto inserted = files.insert(WindowsPath(cmd_i.first));
                Y_ASSERT(inserted.second);
                curNode.SetAttr("Include", *inserted.first);
                TString msg;
                THashSet<TString> inputs;
                TString iStr;
                THashSet<TString> outputs;
                TString oStr;
                for (size_t i = 0; i < cmds.size(); ++i) {
                    TString cmdName;
                    cmds[i].Name.GetStr(cmdName);
                    msg += cmdName + '\t';
                    for (const auto& na : cmds[i].Inputs()) {
                        if (inputs.insert(na.Name).second) {
                            iStr += WindowsPath(na.Name) + ';';
                        }
                    }
                    for (const auto& na : cmds[i].Tools()) {
                        if (inputs.insert(na.Name).second) {
                            iStr += WindowsPath(na.Name) + ';';
                        }
                    }
                    for (const auto& na : cmds[i].Outputs()) {
                        if (outputs.insert(na.Name).second) {
                            oStr += WindowsPath(na.Name) + ';';
                        }
                    }
                }
                curNode.AddChild("Message", msg);
                curNode.AddChild("AdditionalInputs", iStr);
                curNode.AddChild("Outputs", oStr);
                curNode = curNode.Node("..");
                for (size_t i = 0; i < cmds.size(); ++i) {
                    bool global = module.FileGlobal(cmds[i].Id());
                    for (const auto& na : cmds[i].Outputs()) {
                        // While .obj files are added to link/lib from outputs, .o are not
                        if (IsOObjPath(na.Name)) {
                            curNode = curNode.AddChild("Object");
                            curNode.SetAttr("Include", WindowsPath(na.Name));
                            curNode = curNode.Node("..");
                        }
                        if (global && IsObjPath(na.Name)) {
                            // cmds are based on FileIds, so we can safely construct TFile from them
                            // na.Name here contains path (most probably output), so don't add intDir here
                            GlobalObjPool.Add({YMake, cmds[i].Id()}, {na.Name, ""});
                        }
                    }
                }
            }

            // Add headers
            TFiles includes = module.FilterFiles("h", NPath::Source);
            if (!includes.empty()) {
                bool isGlobal = module.IsGlobalModule();
                for (const auto& include: includes) {
                    auto includeWinName = include.NameAbsWin();
                    if (isGlobal && !sourcesWithoutExt.contains(NPath::NoExtension(includeWinName))) {
                        continue;
                    }
                    auto inserted = files.insert(includeWinName);
                    if (!inserted.second) {
                        continue;
                    }
                    curNode = curNode.AddChild("ClInclude");
                    curNode.SetAttr("Include", *inserted.first);
                    curNode = curNode.Node("..");
                }
            }

            // Adding project references
            curNode = renderer.Root.AddChild("ItemGroup");
            TModules reqs;
            if (peers) {
                ToSortedData(reqs, *peers, YMake);
            }
            if (tools) {
                ToSortedData(reqs, *tools, YMake);
            }
            if (::IsIn({TModule::K_PROGRAM, TModule::K_DLL}, module.Kind())) {
                ToSortedData(reqs, GlobalNodes, YMake);
            }
            for (const auto& req : reqs) {
                curNode = curNode.AddChild("ProjectReference");
                curNode.SetAttr("Include", MsvsProjectPath(req.LongName()));
                curNode.AddChild("Project", TString("{") + GenGuid(req) + "}");
                curNode = curNode.Node("..", false, renderer.Namespaces);
            }

            bool modified = false;

            bool vcxprojModified = renderer.Finish(RealProjectPath(path));
            modified |= vcxprojModified;
            if (YMake.Conf.VerboseMake && vcxprojModified) {
                Cerr << "Modified " << RealProjectPath(path) << Endl;
            }

            DisableFilters(path);

            ++Stats.NumProjects;
            Stats.NumUpdatedProjects += modified;
        }

        void TVcRender::DisableFilters(const TStringBuf& path) {
            TString filtersPath = RealProjectPath(path) + ".filters";
            if (!NFs::Exists(filtersPath)) {
                return;
            }
            if (NFs::Rename(filtersPath, filtersPath + ".backup")) {
                if (YMake.Conf.VerboseMake) {
                    Cerr << "Disabled " << filtersPath << Endl;
                }
            }
        }

        void TVcRender::RenderAllVcxproj(const TStringBuf& title, const TStringBuf& path, const TStringBuf& guid) {
            YDIAG(V) << "rendering ALL vcxproj" << Endl;

            TVcxprojRenderer renderer("all_proj.xml");

            renderer.AddProjectConfigurations();
            renderer.AddGlobals(title, guid, Version);
            renderer.AddConfiguration("Utility", GetPlatformToolset(), GetToolsVersion());

            if (UseArcadiaToolchain) {
                renderer.AddArcadiaProps();
            }

            NXml::TNode node = renderer.Root.Node("//ms:PropertyGroup[not(@*)]", false, renderer.Namespaces);
            node.AddChild("OutDir", WindowsPath(IntermediateDir) + "\\");
            node.AddChild("IntDir", WindowsPath(NPath::Join(IntermediateDir, guid)) + "\\");

            node = renderer.Root.Node("//ms:ItemGroup[not(@*)]", false, renderer.Namespaces);
            for (const auto& modInfo : ModSlnInfo) {
                node = node.AddChild("ProjectReference");
                node.SetAttr("Include", MsvsProjectPath(modInfo.LongName()));
                node.AddChild("Project", GenGuid(modInfo));
                node = node.Node("..", false, renderer.Namespaces);
            }
            renderer.Root.AddChild("ItemGroup").AddChild("Natvis").SetAttr("Include", "arcadia.natvis");

            TString filename = RealProjectPath(path);
            bool modified = renderer.Finish(filename);

            TString natvisPath = RealProjectDirPath("arcadia.natvis");
            {
                TFileOutput natvisOutput(natvisPath);
                natvisOutput.Write(NResource::Find("arcadia.natvis"));
            }

            if (YMake.Conf.VerboseMake && modified) {
                Cerr << "Modified " << filename << " and " << natvisPath << Endl;
            }
        }

        void TVcRender::RenderSln() {
            Y_ASSERT(IntermediateDir);
            NDev::TModifiedFile sln(SolutionFilename());

            const TProjectTree::TNode& allProjectNode = ProjectTree.Add(AllProject);
            TString allProjectGuid = allProjectNode.Guid();
            RenderAllVcxproj(allProjectNode.Name, allProjectNode.Path, allProjectGuid);

            ProjectTree.Sort();

            sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
            ProjectTree.Traverse([&](const TProjectTree::TNode& node) {
                TString path;
                if (node.Type == TProjectTree::TNode::EType::T_PROJECT) {
                    path = ProjectPath(node.Path);
                } else {
                    path = ProjectDirPath(node.Path);
                }
                sln << "Project(\"{" << GenGuid(node.Type) << "}\") = "
                    << "\"" << node.Name << "\", "
                    << "\"" << path << "\", "
                    << "\"{" << node.Guid() << "}\""
                    << "\n";
                sln << "EndProject\n";
                return true;
            }, false /* !flat */);

            sln << "Global\n";
            sln << "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
            sln << "\t\tDebug|x64 = Debug|x64\n";
            sln << "\t\tRelease|x64 = Release|x64\n";
            sln << "\tEndGlobalSection\n";
            sln << "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
            ProjectTree.Traverse([&sln](const TProjectTree::TNode& node) {
                if (node.Type != TProjectTree::TNode::EType::T_PROJECT) {
                    return true;
                }
                TString guid = node.Guid();
                sln << "\t\t{" << guid << "}.Debug|x64.ActiveCfg = Debug|x64\n";
                sln << "\t\t{" << guid << "}.Debug|x64.Build.0 = Debug|x64\n";
                sln << "\t\t{" << guid << "}.Release|x64.ActiveCfg = Release|x64\n";
                sln << "\t\t{" << guid << "}.Release|x64.Build.0 = Release|x64\n";
                return true;
            });
            sln << "\tEndGlobalSection\n";
            sln << "\tGlobalSection(NestedProjects) = preSolution\n";
            ProjectTree.Traverse([&](const TProjectTree::TNode& node) {
                const TProjectTree::TNode& parent = ProjectTree.GetParent(node, false /* !flat */);
                if (parent.Type == TProjectTree::TNode::EType::T_ROOT) {
                    return true;
                }
                sln << "\t\t{" + node.Guid() + "} = {" + parent.Guid() + "}\n";
                return true;
            }, false /* !flat */);
            sln << "\tEndGlobalSection\n";
            sln << "\tGlobalSection(ExtensibilityGlobals) = postSolution\n";
            sln << "\tEndGlobalSection\n";
            sln << "\tGlobalSection(ExtensibilityAddIns) = postSolution\n";
            sln << "\tEndGlobalSection\n";
            sln << "EndGlobal\n";

            ++Stats.NumSolutions;
        }

        void TVcRender::Leave(TState& state) {
            TBase::Leave(state);
            TStateItem& st = state.Top();
            const auto node = st.Node();

            if (state.HasIncomingDep()) {
                const auto& pst = *(state.Parent());
                auto prevEnt = ((TNodeInfo*)pst.Cookie);
                auto incDep = state.IncomingDep();
                if (pst.Node()->NodeType == EMNT_Directory && *incDep == EDT_Include) {
                    if (!prevEnt->ToolModule && node->NodeType == EMNT_Program) { // only first program is a tool
                        prevEnt->ToolModule = node.Id();
                    }
                } else if (IsDirectPeerdirDep(incDep)) {
                    if (node->NodeType != EMNT_Library) { // FIXME(spreis): We don't support non-library peerdirs by now
                        YConfErr(BadDir) << "Expected library in " << TDepGraph::GetFileName(node) << Endl;
                        if (Diag()->BadDir) {
                            state.DumpW();
                        }
                    } else {
                        AddTo(node.Id(), prevEnt->Peers);
                    }
                } else if (IsTooldirDep(incDep)) {
                    if (!CurEnt->ToolModule) {
                        YConfErr(BadDir) << "Expected tool in " << TDepGraph::GetFileName(node) << Endl;
                        if (Diag()->BadDir) {
                            state.DumpW();
                        }
                    } else {
                        AddTo(CurEnt->ToolModule, prevEnt->Tools);
                    }
                }

                if (!IsTooldirDep(incDep)) {
                    // FIXME(spreis): This collects transitive peerdirs which are not needed in projects,
                    //                however we need GlobalObjs from them. So we need to propagate GlobalObjs
                    //                instead of peerdirs and move this AddTo under condition below it
                    AddTo(CurEnt->Peers, prevEnt->Peers);
                    if (!IsDirectPeerdirDep(incDep)) {
                        AddTo(CurEnt->Tools, prevEnt->Tools);
                    }
                }
            }

            auto isGlobalNode = [this](const TState& state, auto node) {
                if (!state.HasIncomingDep()) {
                    return false;
                }
                auto incDep = state.IncomingDep();
                if (*incDep != EDT_Search2 && node->NodeType != EMNT_NonParsedFile) {
                    return false;
                }
                const auto& pst = *(state.Parent());
                const auto parentModule = YMake.Modules.Get(pst.Node()->ElemId);
                return parentModule && parentModule->GetGlobalLibId() == node->ElemId;
            };

            if (CurEnt->IsFile) {
                if (!CurEnt->ModuleRendered) {
                    if (IsModule(st)) {
                        CurEnt->ModuleRendered = true;
                        TModule module(YMake, node.Id(), node.Id(), Mod2Srcs[node.Id()], true);
                        RenderVcxproj(module, CurEnt->Peers, CurEnt->Tools);
                    } else if (isGlobalNode(state, node)) {
                        CurEnt->ModuleRendered = true;
                        auto parentNodeId = state.Parent()->Node().Id();
                        const auto parentEnt = ((TNodeInfo*)state.Parent()->Cookie);
                        TModule module(YMake, node.Id(), parentNodeId, Mod2Srcs[parentNodeId], false);
                        GlobalNodes.Push(node.Id());
                        RenderVcxproj(module, parentEnt->Peers, parentEnt->Tools);
                    }
                }
            }
        }

        void TVcRender::Render() {
            TFsPath origBuildRoot = YMake.Conf.BuildRoot;
            TFsPath origSourceRoot = YMake.Conf.SourceRoot;
            YMake.Conf.BuildRoot = "$(SolutionDir)$(Configuration)";
            if (auto srcRoot = YMake.Conf.GetExportSourceRoot()) {
                YMake.Conf.SourceRoot = srcRoot;
            }
            IntermediateDir = YMake.Conf.RealPath(BuildPath(INTERMEDIATE_DIR));
            xmlKeepBlanksDefault(0);
            YMake.AssignSrcsToModules(Mod2Srcs);
            IterateAll(YMake.Graph, YMake.StartTargets, *this, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
            Cerr << "Updated " << Stats.NumUpdatedProjects << " projects of " << Stats.NumProjects << Endl;
            RenderSln();
            Cerr << "Rendered " << Stats.NumSolutions << " solution files" << Endl;
            IntermediateDir.clear();
            YMake.Conf.BuildRoot = origBuildRoot;
            YMake.Conf.SourceRoot = origSourceRoot;
        }

        TString TVcRender::InternalProjectDirPath(const TStringBuf& path) const {
            return NPath::Join(PROJECTS_DIR, path);
        }

        TString TVcRender::InternalProjectPath(const TStringBuf& path) const {
            return InternalProjectDirPath(path) + ".vcxproj";
        }

        TString TVcRender::ProjectDirPath(const TStringBuf& path) const {
            return WindowsPath(InternalProjectDirPath(path));
        }

        TString TVcRender::ProjectPath(const TStringBuf& path) const {
            return WindowsPath(InternalProjectPath(path));
        }

        TString TVcRender::MsvsProjectPath(const TStringBuf& path) const {
            return TString("$(SolutionDir)") + ProjectPath(path);
        }

        TString TVcRender::RealProjectPath(const TStringBuf& path) const {
            return NPath::Join(SolutionRoot.c_str(), InternalProjectPath(path));
        }

        TString TVcRender::RealProjectDirPath(const TStringBuf& path) const {
            return NPath::Join(SolutionRoot.c_str(), InternalProjectDirPath(path));
        }

        TString TVcRender::SolutionFilename() const {
            return NPath::Join(SolutionRoot.c_str(), Name + ".sln");
        }

        TString TVcRender::GetPlatformToolset() const {
            bool useClang = ::IsTrue(YMake.Conf.CommandConf.EvalValue("CLANG_CL"));
            return useClang ? LLVMToolset : PlatformToolSet(Version);
        }

        TString TVcRender::GetToolsVersion() const {
            TString version;
            if (auto rawValue = YMake.Conf.CommandConf.Get1("MSVS_TOOLS_VERSION")) {
                return TString(GetCmdValue(rawValue));
            } else {
                return {};
            }
        }
    }
}
