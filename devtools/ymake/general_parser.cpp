#include "general_parser.h"

#include "add_node_context_inline.h"
#include "glob_helper.h"
#include "makefile_loader.h"
#include "macro_string.h"
#include "macro_processor.h"
#include "module_loader.h"
#include "module_resolver.h"
#include "prop_names.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/trace.h>

#include <devtools/ymake/common/split.h>
#include <devtools/ymake/common/uniq_vector.h>

#include <devtools/ymake/lang/resolve_include.h>

#include <util/folder/pathsplit.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>

#include <utility>

#define SBDIAG YDIAG(SUBST) << MsgPad

namespace {
    TStringBuf MsgPad; // debug only

    void ProcessMakefileGlobs(TNodeAddCtx& node, const TVector<TModuleDef*>& modules) {
        for (const auto module : modules) {
            THashMap<ui32, TVector<ui32>> globVarElemId2PatternElemIds;
            for (const auto& globInfo : module->GetModuleGlobs()) {
                // EMNT_Makefile -> GlobCmd -> WatchDirs
                const auto globPatternElemId = globInfo.GlobPatternId;
                globVarElemId2PatternElemIds[globInfo.ReferencedByVar].push_back(globPatternElemId);
                const auto emnt = EMNT_BuildCommand;
                node.AddUniqueDep(EDT_Property, emnt, globPatternElemId);
                auto& [id, entryStats] = *node.UpdIter.Nodes.Insert(MakeDepsCacheId(emnt, globPatternElemId), &node.YMake, node.Module);
                auto& globNode = entryStats.GetAddCtx(node.Module, node.YMake);
                globNode.NodeType = emnt;
                globNode.ElemId = globPatternElemId;
                entryStats.SetOnceEntered(false);
                entryStats.SetReassemble(true);
                PopulateGlobNode(globNode, globInfo);
            }
            for (auto& [globVarElemId, globPatternElemIds]: globVarElemId2PatternElemIds) {
                TGlobHelper::SaveGlobPatternElemIds(module->GetModuleGlobsData(), globVarElemId, std::move(globPatternElemIds));
            }
        }
    }

    void ProcessNeverCacheModules(TNodeAddCtx& node, std::span<const TModuleDef* const> modules) {
        const bool hasNevercacheMods = std::ranges::any_of(modules, [](const TModuleDef* mod) {return mod->IsNeverCache();});
        if (hasNevercacheMods) {
            const auto elem = node.UpdIter.Graph.Names().AddName(EMNT_Property, NProps::NEVERCACHE_PROP);
            node.AddUniqueDep(EDT_Property, EMNT_Property, elem);
        }
    }

    bool IsGlobMatchDep(const TDepTreeNode& prev, EDepType dep, const TNodeAddCtx& node) {
        if (prev.NodeType != EMNT_BuildCommand || dep != EDT_Property || node.NodeType != EMNT_File) {
            return false;
        }
        ui64 id;
        TStringBuf name, val;
        ParseCommandLikeProperty(node.Graph.GetCmdName(prev.NodeType, prev.ElemId).GetStr(), id, name, val);
        return name == NProps::LATE_GLOB || name == NProps::GLOB;
    }

    void ProcessMakefileInclude(TNodeAddCtx& node, TStringBuf incFile, bool useTextContext = false) {
        auto& fileConf = node.YMake.Names.FileConf;
        EMakeNodeType type;
        ui32 elemId;
        TFileView incView = fileConf.GetStoredName(incFile);
        fileConf.MarkAsMakeFile(incView);
        if (fileConf.YPathExists(incView, EPathKind::File)) {
            type = EMNT_File;
            if (useTextContext) {
                elemId = fileConf.ConstructLink(ELinkType::ELT_Text, incView).GetElemId();
            } else {
                elemId = incView.GetElemId();
            }
            auto fileContentHolder = fileConf.GetFileById(elemId);
            fileContentHolder->UpdateContentHash();
            fileContentHolder->ValidateUtf8(incView.GetTargetStr());
        } else {
            // We already reported BadFile error in DirParser::OnInclude
            type = EMNT_MissingFile;
            elemId = fileConf.Add(NPath::SetType(incFile, NPath::Unset));
        }
        node.AddUniqueDep(EDT_Include, type, elemId);
    }

    constexpr std::pair<TStringBuf, bool> additionalDepsDescriptions[] = {
        { TStringBuf("_MAKEFILE_INCLUDE_LIKE_DEPS"), false },
        { TStringBuf("_MAKEFILE_INCLUDE_LIKE_TEXT_DEPS"), true }
    };

    void ProcessMakefileIncludes(TNodeAddCtx& node, const TVector<TModuleDef*>& modules, const TVector<TString>& includes) {
        for (const auto& include: includes) {
            ProcessMakefileInclude(node, include);
        }

        if (modules.size()) {
            TFileView makefile = node.Graph.Names().FileConf.ResolveLink(node.Graph.GetFileName(node.ElemId));
            for (const auto module : modules) {
                for (const auto& [varName, useTextContext] : additionalDepsDescriptions) {
                    const auto additionalDeps = StringSplitter(module->GetVars().EvalValue(varName)).Split(' ').SkipEmpty();
                    for (const auto& dep : additionalDeps) {
                        const auto resolvedDep = ResolveIncludePath(dep, makefile.GetTargetStr());
                        ProcessMakefileInclude(node, resolvedDep, useTextContext);
                    }
                }
            }
        }
    }

    void ApplySelfPeers(const TVector<TModuleDef*>& modules) {
        THashMap<TStringBuf, ui32> moduleMap;
        for (const auto mod : modules) {
            moduleMap[mod->GetModuleConf().Tag] = mod->GetModule().GetId();
        }

        for (auto mod : modules) {
            TUniqVector<ui32> selfPeers;
            for (const auto& tag : mod->GetModuleConf().SelfPeers) {
                if (moduleMap.contains(tag)) {
                    selfPeers.Push(moduleMap[tag]);
                }
            }
            mod->GetModule().SelfPeers = selfPeers.Take();
        }
    }

    TGeneralParser::TModuleConstraints MakeModuleConstraintChecker(const TVars& confVars) {
        if (!confVars.Contains("EXPLICIT_VERSION_PREFIXES"))
            return [](const TModuleDef&){};
        TVector<TStringBuf> prefixes, exceptions;
        for (TStringBuf prefix: SplitBySpace(GetCmdValue(confVars.Get1("EXPLICIT_VERSION_PREFIXES")))) {
            prefixes.push_back(prefix);
        }
        for (TStringBuf except: SplitBySpace(GetCmdValue(confVars.Get1("EXPLICIT_VERSION_EXCEPTIONS")))) {
            exceptions.push_back(except);
        }

        return [prefixes = std::move(prefixes), exceptions = std::move(exceptions)](const TModuleDef& modDef) {
            const TStringBuf moddir = modDef.GetModule().GetDir().CutType();
            const auto isModPrefix = [moddir](TStringBuf prefix) {return NPath::IsPrefixOf(prefix, moddir);};
            if (!modDef.IsVersionSet() && modDef.GetModule().Get("SKIP_VERSION_REQUIREMENTS") != "yes") {
                auto it = std::ranges::find_if(prefixes, isModPrefix);
                if (it != prefixes.end() && !std::ranges::any_of(exceptions, isModPrefix)) {
                    TScopedContext context(modDef.GetModule().GetName());
                    YConfErr(Misconfiguration) << "Explicit VERSION must be specified for modules inside " << *it << " directory" << Endl;
                }
            }
        };
    }
}

TFileView MakefileNodeNameForDir(TFileConf& fileConf, TFileView dir) {
    TFileView makefile = fileConf.GetStoredName(NPath::SmartJoin(dir.GetTargetStr(), "ya.make"));
    return TFileConf::ConstructLink(ELinkType::ELT_MKF, makefile);
}

TFileView MakefileNodeNameForModule(const TFileConf& fileConf, const TModule& module) {
    return fileConf.ConstructLink(ELinkType::ELT_MKF, module.GetMakefile());
}

TGeneralParser::TGeneralParser(TYMake& yMake)
    : YMake(yMake)
    , Graph(yMake.Graph)
    , Conf(yMake.Conf)
    , ModuleConstraintsChecker(MakeModuleConstraintChecker(yMake.Conf.CommandConf))
    , YaMakeContentProvider(yMake.Names.FileConf)
{
}

void TGeneralParser::RelocateFile(TNodeAddCtx& node, TStringBuf newName, EMakeNodeType newType, TDGIterAddable& iterAddable) {
    ui32 id = YMake.Names.FileConf.Add(newName);
    RelocateFile(node, id, newType, iterAddable);
}

void TGeneralParser::RelocateFile(TNodeAddCtx& node, ui32 newElemId, EMakeNodeType newType, TDGIterAddable& iterAddable) {
    auto originalCacheId = MakeDepsCacheId(node.NodeType, node.ElemId);
    node.ElemId = iterAddable.Node.ElemId = newElemId;
    node.NodeType = newType;
    RelocatedNodes[originalCacheId] = TDepTreeNode(node.NodeType, node.ElemId);
    // Edit existing graph node if there is one
    const auto oldUpdNode = node.UpdNode;
    node.UpdNode = iterAddable.NodeStart = Graph.GetNodeById(node.NodeType, node.ElemId).Id();
    if (node.UpdNode != TNodeId::Invalid && node.UpdNode != oldUpdNode) {
        node.UseOldDeps();
    }
}

bool TGeneralParser::NeedUpdateFile(ui64 fileId, EMakeNodeType type, TFileHolder& fileContent) {
    if (type == EMNT_MissingFile) {
        return true;
    }

    if (type == EMNT_NonProjDir) {
        return Graph.GetFileName(fileId).IsType(NPath::Source);
    }

    if (!fileContent) {
        fileContent.Reset(Graph.Names().FileConf.GetFileById(fileId));
    }

    auto changed = fileContent->CheckForChanges(Conf.NoParseSrc ? ECheckForChangesMethod::RELAXED : ECheckForChangesMethod::PRECISE);

    YDIAG(GUpd) << "NeedUpdate check: " << fileId << ": " << changed << Endl;
    if (Y_UNLIKELY(changed && Diag()->FU)) {
        Cerr << "UPD: " << Graph.GetFileName(type, fileId) << Endl;
    }

    return changed;
}

void TGeneralParser::ProcessFile(TFileView name, TNodeAddCtx& node, TAddIterStack& stack, TFileHolder& fileContent, TModule* mod) {
    YDIAG(DG) << "Adding " << name << " (" << node.NodeType << ") to DepGraph:" << Endl;
    YMake.InitPluginsAndParsers();

    auto& nodeEntry = stack.back().EntryPtr->second;

    bool fileNotFound = false;
    auto& fileConf = Graph.Names().FileConf;
    if (node.NodeType == EMNT_MakeFile || node.NodeType == EMNT_MissingFile || node.NodeType == EMNT_File) {
        if (name.IsType(NPath::Unset) || name.IsType(NPath::Build)) {
            Y_ASSERT(!name.IsType(NPath::Build)); // This should be EMNT_UnknownFile
            // TODO: search again
            fileNotFound = true;
        } else {
            if (!fileContent) {
                fileContent = fileConf.GetFileById(node.ElemId);
            }
            fileNotFound = fileContent->IsNotFound();
        }
    }

    auto prev = stack.size() >= 2 ? &stack[stack.size() - 2] : nullptr;

    switch (node.NodeType) {
        case EMNT_MissingDir:
        case EMNT_NonProjDir:
        case EMNT_Directory:
            node.NodeType = ProcessDirectory(name, node);
            break;
        case EMNT_MakeFile: {
            // TODO: do not parse if md5 didn't change
            Y_ASSERT(IsAtDirMkfDep(stack));
            Y_ASSERT(name.GetContextType() == ELinkType::ELT_MKF);
            prev->Entry().SetReassemble(true);
            TFileView fileName = fileConf.ResolveLink(name);
            fileConf.MarkAsMakeFile(fileName);
            YDIAG(Dev) << "parse set for " << name << Endl;

            if (fileNotFound) {
                if (fileConf.GetFileDataById(node.ElemId).Changed) {
                    nodeEntry.Props.SetIntentNotReady(EVI_GetModules, YMake.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                }
                break;
            }

            try {
                ProcessMakeFile(fileName, node);
            } catch (TFileError& e) {
                TScopedContext context(fileName);
                YConfErr(BadFile) << "Unable to parse a make file '" << fileName << "': " << LastSystemErrorText(e.Status()) << " (" << e.Status() << ")";
                NEvent::TInvalidFile event;
                event.SetFile(TString{fileName.GetTargetStr()});
                TRACE(P, event);
            }
        } break;
        case EMNT_MissingFile: // they come here for retries
            if (prev && IsMakeFileIncludeDep(prev->Node.NodeType, prev->Dep.DepType, node.NodeType)) {
                // Special case: no module to provide context
                TFileView incView = fileConf.ReplaceRoot(name, NPath::Source);
                if (fileConf.YPathExists(incView, EPathKind::File)) {
                    RelocateFile(node, incView.GetElemId(), EMNT_File, stack.back());
                } else {
                    nodeEntry.HasChanges = false;
                }
                break;
            }

            if (fileConf.ResolveLink(name).IsType(NPath::Unset)) {
                if (prev && IsIncludeFileDep(prev->Node.NodeType, prev->Dep.DepType, node.NodeType)) {
                    // Includes shouldn't be resolved as Sources. Skip it.
                    // Second resolving attempt will be performed by TIncFixer.
                    break;
                }

                TString nameStr;
                name.GetStr(nameStr);
                TVarStrEx src(nameStr); // need path with context for resolving
                TModuleResolver resolver(*mod, YMake.Conf, YMake.GetModuleResolveContext(*mod));
                resolver.ResolveSourcePath(src, {}, TModuleResolver::Default);
                if (!src.IsPathResolved) {
                    nodeEntry.HasChanges = false;
                    break;
                }
                Y_ASSERT(src.ElemId); // IsPathResolved=true here and ElemId must be filled too
                RelocateFile(node, src.ElemId, src.IsOutputFile ? EMNT_NonParsedFile : EMNT_File, stack.back());
                if (mod->IsLoaded() && !IsPropertyValue(stack)) {
                    nodeEntry.Props.SetIntentNotReady(EVI_ModuleProps, YMake.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                }
                if (node.UpdNode != TNodeId::Invalid) {
                    YDIAG(V) << "Node was found! " << name << " -> " << src.Name << Endl;
                    break;
                }
                YDIAG(V) << "File was found! " << name << " -> " << src.Name << Endl;
                if (src.IsOutputFile) {
                    break;
                }
                fileContent = Graph.Names().FileConf.GetFileById(src.ElemId);
                fileContent->UpdateContentHash();
            }
            YMake.IncParserManager.ProcessFile(*fileContent, YMake.GetFileProcessContext(mod, node));
            break;
        case EMNT_File:
            if (prev && IsMakeFileIncludeDep(prev->Node.NodeType, prev->Dep.DepType, node.NodeType)) {
                // Special case: no module to provide context
                break;
            }
            if (prev && IsGlobMatchDep(prev->Node, prev->Dep.DepType, node)) {
                // Special case: no module to provide context
                break;
            }

            node.AddInputs();
            if (node.NodeType == EMNT_NonParsedFile) { // AddInputs() can change node type
                break;
            }

            if (fileNotFound) {
                YDIAG(V) << "File is missing! " << name << Endl;
                TFileView newName;
                if (name.IsType(NPath::Link)) {
                    TFileView targetName = fileConf.ReplaceRoot(fileConf.ResolveLink(name), NPath::Unset);
                    newName = fileConf.ConstructLink(name.GetContextType(), targetName);
                } else {
                    newName = fileConf.ReplaceRoot(name, NPath::Unset);
                }
                RelocateFile(node, newName.GetElemId(), EMNT_MissingFile, stack.back());
                if (mod->IsLoaded() && !IsPropertyValue(stack)) {
                    nodeEntry.Props.SetIntentNotReady(EVI_ModuleProps, YMake.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                }
                break;
            }
            YMake.IncParserManager.ProcessFile(*fileContent, YMake.GetFileProcessContext(mod, node));
            break;
        case EMNT_Program:
        case EMNT_Library:
        case EMNT_Bundle:
        case EMNT_NonParsedFile:
            node.AddInputs();
            break;
        case EMNT_Deleted:
        default:
            YErr() << "[&&&] TGeneralParser::ProcessFile internal error: EMNT_type::??? support is not implemented yet.";
            Y_ASSERT(false);
            break;
    }
}

void TGeneralParser::ProcessCommand(TCmdView cmdView, TNodeAddCtx& node, TAddIterStack& stack) {
    YDIAG(DG) << "Adding " << cmdView << " (" << node.NodeType << ") to DepGraph:" << Endl;

    switch (node.NodeType) {
        case EMNT_BuildCommand:
            if (YMake.Commands.GetByElemId(node.ElemId)) {
                AddCommandNodeDeps(node);
            } else if (IsPropDep(stack)) {
                ProcessCmdProperty(cmdView.GetStr(), node, stack); // NOTE: node.DepsToInducedProps() is already called
            } else {
                ProcessBuildCommand(cmdView.GetStr(), node, stack);
            }
            break;
        case EMNT_UnknownCommand:
            // TODO: recover it next time
            break;
        case EMNT_BuildVariable:
            // TODO: NOP?
            break;
        case EMNT_Property:
            ProcessProperty(cmdView.GetStr(), node, stack);
            break;
        case EMNT_Deleted:
        default:
            YErr() << "[&&&] TGeneralParser::ProcessComand internal error: EMNT_type::??? support is not implemented yet.";
            Y_ASSERT(false);
            break;
    }
}

EMakeNodeType TGeneralParser::DirectoryType(TFileView dirView) const {
    auto& fileConf = Graph.Names().FileConf;
    if ((!dirView.IsType(NPath::Source)) ||
        (!fileConf.YPathExists(dirView, EPathKind::Dir))) {
        return EMNT_MissingDir;
    }
    return fileConf.DirHasYaMakeFile(dirView)
        ? EMNT_Directory
        : EMNT_NonProjDir;
}

EMakeNodeType TGeneralParser::ProcessDirectory(TFileView dirname, TNodeAddCtx& node) {
    auto dirType = DirectoryType(dirname);

    if (dirType == EMNT_Directory) {
        if (node.UpdNode != TNodeId::Invalid) {
            YDIAG(Dev) << "ProcessDirectory " << dirname << ": sz = " << node.Deps.Size() << Endl;
            node.Deps.Clear(); // we rewrite all records
        }

        const auto makefile = MakefileNodeNameForDir(YMake.Names.FileConf, dirname);
        YDIAG(DG) << "MakeFile dep: " << makefile << Endl;
        node.AddDep(EDT_Include, EMNT_MakeFile, makefile.GetElemId());
    }

    return dirType;
}

void TGeneralParser::ReportStats() {
    Stats.Set(NStats::EGeneralParserStats::UniqueCount, YaMakeContentProvider.UniqCount());
    Stats.Set(NStats::EGeneralParserStats::Size, YaMakeContentProvider.ReadSize());
    Stats.Set(NStats::EGeneralParserStats::Includes, YaMakeContentProvider.ReadCount() - Stats.Get(NStats::EGeneralParserStats::Count));
    Stats.Set(NStats::EGeneralParserStats::UniqueSize, YaMakeContentProvider.UniqSize());
    Stats.Report();
}

void TGeneralParser::AddCommandNodeDeps(TNodeAddCtx& node) {
    const auto toolsAndStuff = YMake.Commands.GetCommandToolsEtc(node.ElemId);
    for (const auto& toolValue : toolsAndStuff.Tools) {
        // lifted from tool handling in ProcessBuildCommand
        TString tool = NPath::IsExternalPath(toolValue) ? TString{toolValue} : NPath::ConstructYDir(toolValue, TStringBuf(), ConstrYDirDiag);
        SBDIAG << "Tool dep: " << tool << Endl;
        node.AddUniqueDep(EDT_Include, EMNT_Directory, tool);
    }
    for (const auto& resultValue : toolsAndStuff.Results) {
        // lifted from tool handling in ProcessBuildCommand
        TString tool = NPath::IsExternalPath(resultValue) ? TString{resultValue} : NPath::ConstructYDir(resultValue, TStringBuf(), ConstrYDirDiag);
        SBDIAG << "Result dep: " << tool << Endl;
        node.AddUniqueDep(EDT_Include, EMNT_Directory, tool);
        Graph.Names().CommandConf.GetById(TVersionedCmdId(node.ElemId).CmdId()).KeepTargetPlatform = true;
        YDebug() << "TGeneralParser::AddCommandNodeDeps: KeepTargetPlatform is set for " << node.GetEntry().DumpDebugNode() << " due to " << tool << Endl;
    }

    // a dirty copy-paste from ProcessBuildCommand
    // TODO: move this one level upper since it's common code for all implementations
    bool depsChanged = false;
    if (node.UpdNode != TNodeId::Invalid) {
        TDeps oldDeps;
        node.GetOldDeps(oldDeps, 0, false);
        const auto& delayedDeps = node.UpdIter.DelayedSearchDirDeps.GetNodeDepsByType({node.NodeType, static_cast<ui32>(node.ElemId)}, EDT_Search);
        if (oldDeps.Size() != node.Deps.Size() + delayedDeps.size()) {
            depsChanged = true;
        } else {
            for (size_t n = 0; n < node.Deps.Size(); n++) {
                if (oldDeps[n].ElemId != node.Deps[n].ElemId) {
                    depsChanged = true;
                    break;
                }
            }
            if (!depsChanged) {
                size_t n = 0;
                for (const auto& dirId : delayedDeps) {
                    if (oldDeps[node.Deps.Size() + n].ElemId != dirId) {
                        depsChanged = true;
                        break;
                    }
                    n++;
                }
            }
        }
    }
    node.UpdCmdStampForNewCmdNode(Graph.Names().CommandConf, YMake.TimeStamps, depsChanged);
}

void TGeneralParser::ProcessMakeFile(TFileView resolvedName, TNodeAddCtx& node) {
    auto& fileConf = Graph.Names().FileConf;
    auto elemId = resolvedName.GetElemId();
    YMake.Modules.NotifyMakefileReparsed(elemId);

    auto entries = YMake.Modules.ExtractSharedEntries(elemId);
    if (entries) {
        for (auto fileId: *entries) {
            auto it = YMake.UpdIter->Nodes.find(MakeDepFileCacheId(fileId));
            if (it != YMake.UpdIter->Nodes.end()) {
                it->second.MarkedAsUnknown = true;
                YDIAG(Dev) << fileConf.GetName(fileId) << " was previously in OwnEntries of " << resolvedName << " and now is marked as unknown" << Endl;
            }
        }
    }

    YDIAG(Dev) << "ProcessMakeFile: " << resolvedName << Endl;
    Stats.Inc(NStats::EGeneralParserStats::Count);
    ConfMsgManager()->Erase(node.ElemId);
    ConfMsgManager()->Erase(elemId);

    TPropValues modules, recurses, testRecurses, dirProps;

    TDirParser parser(YMake, fileConf.Parent(resolvedName), resolvedName.GetTargetStr(), modules, recurses, testRecurses, &YaMakeContentProvider);
    parser.Load();
    for (const auto* modDef: parser.GetModules()) {
        ModuleConstraintsChecker(*modDef);
    }

    if (const auto multiModuleName = parser.GetMultiModuleName(); !multiModuleName.empty()) {
        TDepsCacheId propId = MakeDepsCacheId(EMNT_Property, Graph.Names().AddName(EMNT_Property, FormatProperty(NProps::MULTIMODULE, multiModuleName)));
        dirProps.Push(propId);
        if (const auto it = Conf.BlockData.find(multiModuleName); it != Conf.BlockData.end()) {
            if (it->second.HasPeerdirSelf) {
                ApplySelfPeers(parser.GetModules());
            }
        }
    }

    TPropertyType recursesPropType{Graph.Names(), EVI_GetModules, NProps::RECURSES};
    TPropertyType testRecursesPropType{Graph.Names(), EVI_GetModules, NProps::TEST_RECURSES};

    TPropertiesState& props = node.GetEntry().Props;
    TPropertySourceDebugOnly propSourceDebug{EPropertyAdditionType::Created};
    props.ClearValues();
    props.SetValues(TPropertyType{Graph.Names(), EVI_GetModules, "MODULES"}, std::move(modules), propSourceDebug);
    props.SetValues(recursesPropType, std::move(recurses), propSourceDebug);
    props.SetValues(testRecursesPropType, std::move(testRecurses), propSourceDebug);
    props.SetValues(TPropertyType{Graph.Names(), EVI_GetModules, "DIR_PROPERTIES"}, std::move(dirProps), propSourceDebug);
    props.SetIntentReady(EVI_GetModules, YMake.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);

    node.AddDirsToProps(props.GetValues(recursesPropType), TStringBuf(NProps::RECURSES));
    node.AddDirsToProps(props.GetValues(testRecursesPropType), TStringBuf(NProps::TEST_RECURSES));

    if (!parser.GetOwners().empty()) {
        YDIAG(DG) << "Owners dep: " << parser.GetOwners() << Endl;
        node.AddDep(EDT_Property, EMNT_Property, parser.GetOwners());
    }

    ProcessMakefileIncludes(node, parser.GetModules(), parser.GetIncludes());
    ProcessMakefileGlobs(node, parser.GetModules());
    ProcessNeverCacheModules(node, parser.GetModules());
}

void TGeneralParser::ProcessBuildCommand(TStringBuf name, TNodeAddCtx& node, TAddIterStack& stack) {
    ui64 cmdId;
    TStringBuf cmdName, cmdText;
    ParseLegacyCommandOrSubst(name, cmdId, cmdName, cmdText);
    //EMacroType mtype = GetMacroType(cmdText);
    TModule* module = node.Module;
    Y_ASSERT(module->HasId());
    node.GetEntry().SetReassemble(false);

    TVector<TMacroData> tokens;
    // use name!
    GetMacrosFromPattern(name, tokens, true);
    if (cmdName.at(0) == '$') { // special command, such as $LS
        YDIAG(DG) << "PBC special cmd: " << name << Endl;
        if (cmdId) { // TODO: same for other commands
            node.AddUniqueDep(EDT_Include /*EDT_BuildFrom*/, EMNT_MissingFile /*must be EMNT_MissingFile or EMNT_MakeFile*/, cmdId);
        }
        return;
    }

    YDIAG(DG) << "PBC name = " << name << ", " << tokens.size() << " tokens\n";

    const TCommandInfo* cmdInfo = nullptr;

    if (stack.size() >= 2) {
        const auto& prntState = stack[stack.size() - 2];

        if (IsOutputType(prntState.Node.NodeType) && prntState.Dep.DepType == EDT_BuildCommand) {
            TNodeAddCtx* addCtx = prntState.Add.Get();
            if (addCtx) {
                cmdInfo = addCtx->GetModuleData().CmdInfo.Get();
            }

            if (!cmdInfo) {
                ythrow TNotImplemented() << "Output file has no cmd info. Graph seems corrupted. Reconstruct.";
            }
        }
    }

    // All nested macro export "tool" variables resolved from own vars go to the top level
    if (cmdInfo) {
        for (std::span<const TVarStrEx> collection : {cmdInfo->GetTools(), cmdInfo->GetResults()}) {
            for (const auto& cmd : collection) {
                if (!cmd.FromLocalVar) {
                    continue;
                }

                TStringBuf cmdValue = GetCmdValue(cmd.Name);
                TString dir = NPath::IsExternalPath(cmdValue) ? TString{cmdValue} : NPath::ConstructYDir(cmdValue, TStringBuf(), ConstrYDirDiag);

                YDIAG(DG) << (cmd.Result ? "Result" : "Tool") << " dep: " << dir << Endl;
                node.AddUniqueDep(EDT_Include, EMNT_Directory, dir);
                if (cmd.Result) {
                    Graph.Names().CommandConf.GetById(node.ElemId).KeepTargetPlatform = true;
                    YDebug() << "TGeneralParser::ProcessBuildCommand: KeepTargetPlatform is set for " << node.GetEntry().DumpDebugNode() << " due to " << dir << Endl;
                }
            }
        }
    }

    bool depsChanged = false;
    if (node.UpdNode != TNodeId::Invalid) {
        TDeps oldDeps;
        node.GetOldDeps(oldDeps, 0, false);
        if (oldDeps.Size() != node.Deps.Size()) {
            depsChanged = true;
        } else {
            for (size_t n = 0; n < oldDeps.Size(); n++) {
                const TAddDepDescr& oldd = oldDeps[n];
                const TAddDepDescr& newd = node.Deps[n];

                if (oldd.NodeType != newd.NodeType) {
                    depsChanged = true;
                    break;
                }

                if (oldd.DepType != EDT_Property && oldd.NodeType == EMNT_BuildCommand && (oldd.ElemId != newd.ElemId)) {
                    depsChanged = true;
                    break;
                }
            }
        }
    }
    node.UpdCmdStamp(Graph.Names().CommandConf, YMake.TimeStamps, depsChanged);
}

void TGeneralParser::ProcessProperty(TStringBuf /*name*/, TNodeAddCtx& node, TAddIterStack& /* stack& */) {
    if (node.UpdNode == TNodeId::Invalid) {
        Graph.Names().CommandConf.GetById(node.ElemId).CmdModStamp = YMake.TimeStamps.CurStamp();
    }
    //TStringBuf propName = GetPropertyName(name);
    // "OWNER"
    // "Cmd.CfgVars=vars..."
    // "Mod.Nuke=1"
}

void TGeneralParser::ProcessCmdProperty(TStringBuf /*name*/, TNodeAddCtx& node, TAddIterStack& /* stack */) {
    //Graph.Names().CommandConf.GetById(node.ElemId).CmdModStamp = YMake.TimeStamps.CurStamp();
    bool depsChanged = false;
    if (node.UpdNode != TNodeId::Invalid) {
        TDeps oldDeps;
        node.GetOldDeps(oldDeps, 0, false);
        const auto& delayedDeps = node.UpdIter.DelayedSearchDirDeps.GetNodeDepsByType({node.NodeType, static_cast<ui32>(node.ElemId)}, EDT_Search);
        if (oldDeps.Size() != node.Deps.Size() + delayedDeps.size()) {
            depsChanged = true;
        } else {
            for (size_t n = 0; n < node.Deps.Size(); n++) {
                if (oldDeps[n].ElemId != node.Deps[n].ElemId) {
                    depsChanged = true;
                    break;
                }
            }
            if (!depsChanged) {
                size_t n = 0;
                for (const auto& dirId : delayedDeps) {
                    if (oldDeps[node.Deps.Size() + n].ElemId != dirId) {
                        depsChanged = true;
                        break;
                    }
                    n++;
                }
            }
        }
    }
    node.UpdCmdStamp(Graph.Names().CommandConf, YMake.TimeStamps, depsChanged);

    // all was placed in InducedProps - note: of current node, not parent
    // we should handle changes here
    // "Mod.PeerDirs"
    // "Mod.IncDirs"
    // "GlMod.IncDirs"
    // "ParsedIncls.$propName"
}

void PopulateGlobNode(TNodeAddCtx& node, const TModuleGlobInfo& globInfo) {
    node.AddUniqueDep(EDT_Property, EMNT_Property, globInfo.GlobPatternHash);
    for (ui32 fileId: globInfo.MatchedFiles) {
        node.AddUniqueDep(EDT_Property, EMNT_File, fileId);
    }
    for (ui32 exclId: globInfo.Excludes) {
        node.AddUniqueDep(EDT_Property, EMNT_Property, exclId);
    }
    if (globInfo.ReferencedByVar) {
        node.AddUniqueDep(EDT_Property, EMNT_Property, globInfo.ReferencedByVar);
    }
    auto& deps = node.UpdIter.DelayedSearchDirDeps.GetNodeDepsByType({node.NodeType, static_cast<ui32>(node.ElemId)}, EDT_Search);
    deps.clear();
    for (const auto& dir : globInfo.WatchedDirs) {
        deps.Push(dir);
    }
}
