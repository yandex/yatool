#include "action_graph.h"

#include "../add_dep_adaptor.h"
#include "../add_dep_adaptor_inline.h"
#include "../add_iter.h"
#include "../add_node_context_inline.h"
#include "../builtin_macro_consts.h"
#include "../command_store.h"
#include "../conf.h"
#include "../macro_processor.h"
#include "../macro_string.h"
#include "../module_builder.h"
#include "../prop_names.h"
#include "../ymake.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/yexception.h>
#include <util/string/split.h>
#include <util/system/yassert.h>

namespace {
    TVarStrEx* FindMainElemOrDefault(std::span<TVarStrEx> elems, ui32 defaultElemPos) {
        if (elems.empty()) {
            return nullptr;
        }
        auto filterMain = [](const auto& elem) { return elem.Main; };
        auto it = FindIf(elems, filterMain);
        TVarStrEx* mainElem = nullptr;
        if (it != end(elems)) {
            mainElem = &*it;
            if (std::any_of(++it, end(elems), filterMain)) {
                YConfWarn(NoMain) << "Two or more main elements; picked the first one" << Endl;
            }
        } else if (defaultElemPos != Max<ui32>()) {
            if (elems.size() > 1) {
                YConfWarn(NoMain) << "No explicit main element; picked the default one" << Endl;
            }
            mainElem = &elems[defaultElemPos];
        }
        return mainElem;
    }

    EMakeNodeType ActionInputNodeType(const TVarStr& input) {
        return input.IsMacro
            ? EMNT_UnknownCommand
            : input.NotFound
                ? EMNT_MissingFile
                : (input.IsOutputFile ? EMNT_NonParsedFile : EMNT_File);
    }

    TNodeAddCtx& BindingOwner(const TYVar& owner) {
        Y_ENSURE(owner.EntryPtr && owner.EntryPtr->second.AddCtx);
        return *owner.EntryPtr->second.AddCtx;
    }
}

TActionGraphEncoder::TActionGraphEncoder(
    const TBuildConfiguration& conf,
    TDepGraph& graph,
    TUpdIter& updIter,
    TModule* module
)
    : Conf_(conf)
    , Graph_(graph)
    , UpdIter_(updIter)
    , Module_(module)
{
}

TActionGraphEncoder::TActionGraphEncoder(TCommandInfo& commandInfo)
    : TActionGraphEncoder(
        *commandInfo.Conf,
        *commandInfo.Graph,
        *commandInfo.UpdIter,
        commandInfo.Module
    )
{
}

TActionGraphEncoder::TPreparedSubmission::TPreparedSubmission(
    const TActionGraphEncoder& encoder,
    TAddDepAdaptor& storage
)
    : Encoder_(&encoder)
    , Storage_(&storage)
{
}

void TActionGraphEncoder::TPreparedSubmission::AcceptResolvedInput(const TResolvedActionInput& input) {
    Encoder_->RecordResolvedInput(*Storage_, input);
}

TFileElemId TActionGraphEncoder::TPreparedSubmission::InternLogicalPath(TStringBuf path) {
    return Encoder_->InternLogicalPath(path);
}

TActionGraphEncoder::TPreparedSubmission TActionGraphEncoder::PrepareActionSubmission(TAddDepAdaptor& storage) const {
    return TPreparedSubmission(*this, storage);
}

TActionGraphEncoder::TPreparedSubmission TActionGraphEncoder::PrepareVariableSubmission(
    TAddDepAdaptor& storage,
    TCmdElemId variable
) const {
    storage.AddUniqueDep(EDT_Property, EMNT_BuildCommand, variable);
    auto& [id, entryStats] = *UpdIter_.Nodes.Insert(
        MakeDepsCacheId(EMNT_BuildCommand, variable),
        &UpdIter_.YMake,
        Module_
    );
    entryStats.SetOnceEntered(false);
    entryStats.SetReassemble(true);
    auto& variableNode = entryStats.GetAddCtx(Module_, UpdIter_.YMake);
    return TPreparedSubmission(*this, variableNode);
}

TActionGlobEvaluationContext TActionGraphEncoder::MakeGlobEvaluationContext() const {
    return TActionGlobEvaluationContext(Graph_.Names().FileConf, Module_->GetDir().GetElemId());
}

void TActionGraphEncoder::RecordResolvedInput(TAddDepAdaptor& storage, const TResolvedActionInput& input) const {
    if (input.ResolutionRecord) {
        RecordInputResolution(*input.ResolutionRecord);
    }

    const EMakeNodeType inputType = input.IsMacro
        ? EMNT_UnknownCommand
        : input.IsDirectory
            ? EMNT_Directory
            : (input.IsOutput ? EMNT_NonParsedFile : FileTypeByRoot(input.LogicalName));

    TAddDepAdaptor& node = storage.AddOutput(input.File, inputType, false);
    if (input.MarkUsedAsInput) {
        auto& moduleData = node.GetModuleData();
        moduleData.UsedAsInput = true;
        if (moduleData.BadCmdInput) {
            if (input.IsOutput) {
                YConfInfo(BadAuto) << "`noauto' flag needed for " << input.LogicalName << Endl;
            } else {
                YConfInfo(BadAuto) << "file must not be added by extension: " << input.LogicalName << Endl;
            }
            moduleData.BadCmdInput = false; // do not output this multiple times
        }
    }
}

TFileElemId TActionGraphEncoder::InternLogicalPath(TStringBuf path) const {
    return AssumeFile(Graph_.Names().AddName(EMNT_MissingFile, path));
}

void TActionGraphEncoder::RecordInputResolution(const TInputResolutionRecord& resolution) const {
    Y_ENSURE(Module_ != nullptr);
    Module_->ResolveResults.insert({
        resolution.OriginalPath,
        resolution.ResolveDirectory ? resolution.ResolveDirectory : TResolveResult::EmptyPath,
        resolution.ResultPath,
    });
}

TActionGraphEncoder::TImportedCommandExpression TActionGraphEncoder::ImportCommandExpression(
    NPolexpr::TExpression&& expression
) const {
    const TCmdElemId id = UpdIter_.YMake.Commands.Add(Graph_, std::move(expression));
    return {
        .Id = id,
        .Reference = Graph_.Names().CmdNameById(id).GetStr(),
    };
}

TCmdElemId TActionGraphEncoder::InternCommand(
    const TYVar& value,
    EStorageFormat format,
    EExpressionRole role
) const {
    const TCmdElemId elemId = AssumeCmd(Graph_.Names().AddName(EMNT_BuildCommand, Get1(&value)));
    // We can have duplicate command entries even within a single Makefile,
    // e.g. SRCS(foo/a.cpp bar/a.cpp) may turn into `SRCScxx cpp cc=(a.cpp)' for both files.
    // EntryPtr preserves the current registration behavior.
    if (!value.EntryPtr) {
        RegisterCommand(value, elemId, format, role);
    }
    return elemId;
}

void TActionGraphEncoder::RegisterCommand(
    const TYVar& value,
    TCmdElemId elemId,
    EStorageFormat format,
    EExpressionRole role
) const {
    // The dependency graph must not store the legacy SET_APPEND "recursion" convention,
    // where a variable's value is a continuation of itself. Such self-referential data is an
    // internal continuation mechanism only and must be resolved before it reaches the graph;
    // block it (under _DBG_DENY_RECURSIVE_VARS) so that any remaining producers surface.
    if (Conf_.DenyRecursiveVars && role == EExpressionRole::Binding && value.size() == 1 && value[0].HasPrefix) {
        const TStringBuf commandName = value[0].Name;
        // (a) Old-school form: the value literally begins with "$NAME ..."
        // (e.g. "0:MYVAR=$MYVAR ...").
        if (IsSelfReferentialCmd(commandName)) {
            ythrow TError() << "refusing to store self-referential variable into the dependency graph: " << commandName;
        }
        // (b) Struct-cmd form: the value refers to a compiled expression which, when the variable
        // is not inlined (NoInline/NO_EXPAND/reserved), may itself reference NAME as a term.
        if (format == EStorageFormat::Structured) {
            const TStringBuf name = GetCmdName(commandName);
            const TCmdElemId expressionId = Graph_.Names().CommandConf.GetIdNx(GetCmdValue(commandName));
            if (expressionId && UpdIter_.YMake.Commands.CommandReferencesVar(expressionId, name)) {
                ythrow TError() << "refusing to store self-referential variable into the dependency graph: "
                                << commandName << " (compiled expression references " << name << ")";
            }
        }
    }

    if (Conf_.ValidateCmdNodes) {
        Y_ENSURE(value.size() == 1);
        const bool hasPrefix = value[0].HasPrefix;
        const bool isStructured = TVersionedCmdId(elemId).IsNewFormat();
        switch (format) {
            case EStorageFormat::Legacy:
                Y_ENSURE(hasPrefix && !isStructured);
                if (Conf_.DeprecateNonStructCmdNodes) {
                    if (role == EExpressionRole::Action) {
                        throw TNotImplemented() << "old-school commands have been deprecated";
                    }
                    throw TNotImplemented() << "old-school variables have been deprecated";
                }
                break;
            case EStorageFormat::Structured:
                switch (role) {
                    case EExpressionRole::Action:
                        Y_ENSURE(!hasPrefix && isStructured);
                        break;
                    case EExpressionRole::Binding:
                        // Structured variable bindings still use a command-like
                        // wrapper around the structured expression reference.
                        Y_ENSURE(hasPrefix && !isStructured); // TODO: mark it as struct-cmd, too?
                        break;
                }
                break;
        }
    }

    TModule* module = value.Id ? Module_ : UpdIter_.ParentModule;
    TUpdEntryPtr entry = &*UpdIter_.Nodes.Insert(
        MakeDepsCacheId(EMNT_BuildCommand, elemId),
        &UpdIter_.YMake,
        module
    );
    value.EntryPtr = entry;
    entry->second.AddCtx->ElemId = elemId;
    // TODO: Fix this condition. value.Id must be a module id, not a Makefile id.
    if (entry->second.AddCtx->Module == module) {
        entry->second.SetReassemble(true);
        entry->second.SetOnceEntered(false);
    }
}

void TActionGraphEncoder::AttachAction(TAddDepAdaptor& action, TCmdElemId command) const {
    action.AddDepIface(EDT_BuildCommand, EMNT_BuildCommand, command);
}

void TActionGraphEncoder::AttachBinding(
    TAddDepAdaptor& owner,
    TCmdElemId binding,
    EBindingPlacement placement
) const {
    switch (placement) {
        case EBindingPlacement::Local:
            owner.AddUniqueDep(EDT_BuildCommand, EMNT_BuildVariable, binding);
            break;
        case EBindingPlacement::GlobalCompatibility:
            owner.AddUniqueDep(EDT_Include, EMNT_BuildCommand, binding);
            break;
    }
}

void TActionGraphEncoder::RecordReservedVariable(const TYVar& owner, TStringBuf name) const {
    BindingOwner(owner).AddUniqueDep(
        EDT_Property,
        EMNT_Property,
        FormatProperty(NProps::USED_RESERVED_VAR, name)
    );
}

void TActionGraphEncoder::RecordLateOutput(const TYVar& owner, TStringBuf expression) const {
    BindingOwner(owner).AddUniqueDep(
        EDT_Property,
        EMNT_Property,
        FormatProperty(NProps::LATE_OUT, expression)
    );
}

void TActionGraphEncoder::AttachToolDirectory(const TYVar& owner, TStringBuf directory) const {
    BindingOwner(owner).AddUniqueDep(EDT_Include, EMNT_Directory, directory);
}

bool TActionGraphEncoder::AttachLegacyBinding(const TYVar& owner, TCmdElemId binding) const {
    return BindingOwner(owner).AddUniqueDep(EDT_Include, EMNT_BuildCommand, binding);
}

void TActionGraphEncoder::AttachLegacyAction(const TYVar& owner, TCmdElemId action) const {
    BindingOwner(owner).AddDepIface(EDT_Include, EMNT_BuildCommand, action);
}

void TActionGraphEncoder::AttachMissingBinding(const TYVar& owner, TStringBuf name) const {
    BindingOwner(owner).AddDepIface(EDT_Include, EMNT_UnknownCommand, name);
}

TActionGraphEncoder::EActionEncodingResult TActionGraphEncoder::EncodeActionSubmission(
    TAutoPtr<TCommandInfo>& commandInfoOwner,
    TModuleBuilder& modBuilder,
    TPreparedSubmission& submission,
    IActionGlobEvaluator& globEvaluator,
    bool finalTargetCmd
) const {
    TAddDepAdaptor& inputNode = *submission.Storage_;
    auto& commandInfo = *commandInfoOwner;
    auto& Cmd = commandInfo.Cmd;
    auto& GlobalVars = commandInfo.GlobalVars;
    auto& LocalVars = commandInfo.LocalVars;
    auto& MainOutput = commandInfo.MainOutput;
    auto& HasGlobalInput = commandInfo.HasGlobalInput;
    auto GetInput = [&]() { return commandInfo.GetInput(); };
    auto GetOutput = [&]() { return commandInfo.GetOutput(); };
    auto ApplyToOutputIncludes = [&](auto&& action) {
        commandInfo.ApplyToOutputIncludes(std::forward<decltype(action)>(action));
    };
    const auto* Conf = &Conf_;
    auto* Graph = &Graph_;
    auto* UpdIter = &UpdIter_;
    auto* Module = Module_;

    TModule& mod = modBuilder.GetModule();
    Y_ENSURE(UpdIter != nullptr);
    const size_t startCountOuts = finalTargetCmd ? 0 : 1;
    size_t numRealOut = finalTargetCmd ? 1 : 0;

    TVersionedCmdId curCmdId(Cmd.EntryPtr ? AssumeCmd(ElemId(Cmd.EntryPtr->first)) : TCmdElemId());
    TStringBuf curCmdName = Get1(&Cmd);

    TFileConf& fileConf = Graph->Names().FileConf;

    Y_ASSERT(!fileConf.GetName(AssumeFile(inputNode.ElemId)).IsLink());
    TStringBuf inputNodeName = fileConf.GetName(AssumeFile(inputNode.ElemId)).GetTargetStr();

    const auto& ownEntries = mod.GetOwnEntries();
    for (auto& output : GetOutput()) {
        Y_ENSURE(output.IsPathResolved);
        Y_ENSURE(NPath::IsTypedPath(output.Name));

        if (const auto fid = fileConf.GetIdNx(output.Name)) {
            if (ownEntries.has(fid)) {
                YConfErr(DupSrc) << output.Name << " was already added in this project. Skip command: "
                                 << SkipId(curCmdName) << Endl;
                return EActionEncodingResult::Failed;
            }

            // We do not consider outputs of module command that macth main module output as a DupSrc issue
            if (!mod.IgnoreDupSrc() && !(finalTargetCmd && fid == mod.GetId())) {
                if (UpdIter->CheckNodeStatus({EMNT_NonParsedFile, fid}) == NGraphUpdater::ENodeStatus::Ready && !mod.GetSharedEntries().has(fid)) {
                    ConfMsgManager()->AddDupSrcLink(fid, mod.GetId());
                }
            }

            const TNodeId id = Graph->GetFileNodeById(fid).Id();
            if (id == TNodeId::Invalid && !finalTargetCmd && inputNodeName == output.Name) {
                YConfErr(BadOutput) << "The name of intermediate output " << output.Name
                                    << " matches the module name. Skip command: " << SkipId(curCmdName) << Endl;
                return EActionEncodingResult::Failed;
            }

            if (id != TNodeId::Invalid && Graph->GetFileNodeData(fid).NodeModStamp == fileConf.TimeStamps.CurStamp()) {
                TNodeId cmdId = GetDepNodeWithType(id, *Graph, EDT_BuildCommand, EMNT_BuildCommand);
                if (cmdId == TNodeId::Invalid) {
                    // `finalTargetCmd` means either module command or global command, so we cannot just take modBuilder.GetNode().Id
                    TNodeId mainId = GetDepNodeWithType(id, *Graph, EDT_OutTogether, finalTargetCmd ? modBuilder.GetNode().NodeType : EMNT_NonParsedFile);
                    if (finalTargetCmd && mainId == TNodeId::Invalid) {
                        // Second try for global command
                        mainId = GetDepNodeWithType(id, *Graph, EDT_OutTogether, EMNT_NonParsedFile);
                    }
                    Y_ASSERT(mainId != TNodeId::Invalid);
                    cmdId = GetDepNodeWithType(mainId, *Graph, EDT_BuildCommand, EMNT_BuildCommand);
                    Y_ASSERT(cmdId != TNodeId::Invalid);
                }
                const auto cmdView = Graph->GetCmdName(Graph->Get(cmdId));
                if (cmdView.IsNewFormat() || curCmdId.IsNewFormat()) {
                    // TBD
                } else {
                    const auto cmdName = cmdView.GetStr();
                    static constexpr TStringBuf touchCmd = "TOUCH";
                    static constexpr TStringBuf initPy = "__init__.py";
                    if (GetCmdValue(cmdName) != GetCmdValue(curCmdName) &&
                            !(GetCmdName(cmdName) == touchCmd && GetCmdName(curCmdName) == touchCmd && NPath::Basename(output.Name) == initPy)) {
                        YConfErr(BUID) << "Two different commands want to produce the output " << output.Name << ": "
                                        << SkipId(cmdName) << " vs " << SkipId(curCmdName) << Endl;
                    }
                }
            }
        }
        if (!output.ElemId) {
            output.ElemId = Graph->Names().AddName(EMNT_File, output.Name);
            output.OutputInThisModule = true;
        }

        if (output.AddToModOutputs) {
            mod.ExtraOuts.push_back(AssumeFile(output.ElemId));
        }

        // This IncDir should basically enable only resolving of the exact output marked by `addincl` modifier
        // There is no way to apply such precise filtering, but it is OK to add IncDir so late since the
        // resolving subject is added to the graph later in this function.
        if (output.AddToIncl) {
            TStringBuf incl = NPath::Parent(output.Name);
            // TODO: add only if included via "" from .h
            modBuilder.AddIncdir(incl, EIncDirScope::Global, false);
        }
        numRealOut += !output.IsTmp;
    }

    if (!Cmd) {
        if (!finalTargetCmd) {
            modBuilder.QueueCommandOutputs(commandInfo);
        }
        return EActionEncodingResult::Complete;
    }

    if (!numRealOut) {
        YConfErr(NoOutput) << "macro " << SkipId(curCmdName) << " resulted in no outputs, can't add to graph" << Endl;
        return EActionEncodingResult::Failed;
    }

    // Determining the main output.
    TFileElemId mainOutId = TFileElemId();
    EMakeNodeType mainOutType = EMNT_NonParsedFile;
    if (finalTargetCmd) {
        mainOutId = AssumeFile(inputNode.ElemId);
        mainOutType = inputNode.NodeType;
    } else {
        Y_ASSERT(!MainOutput);
        MainOutput = FindMainElemOrDefault(GetOutput(), 0);
        Y_ASSERT(MainOutput);
        MainOutput->IsGlobal = MainOutput->IsGlobal || HasGlobalInput;
        mainOutId = AssumeFile(MainOutput->ElemId);
    }

    TCmdElemId cmdElemId = AssumeCmd(Graph->Names().AddName(EMNT_BuildCommand, curCmdName));

    // 0. Prepare OUTPUT_INCLUDES nodes (ParsedIncls.*)
    THashMap<TStringBuf, TCreateParsedInclsResult> outputIncludeForType;
    ApplyToOutputIncludes([&](TStringBuf type, const TSpecFileArr& outputIncludeArr){
        if (type.empty()) {
            type = "*";
        }

        TVector<TResolveFile> outputIncludes(Reserve(outputIncludeArr.size()));

        for (const auto& outputInclude : outputIncludeArr) {
            YDIAG(DG) << "Include dep: " << outputInclude.Name << " type: " << type << Endl;
            Y_ASSERT(NPath::IsTypedPath(outputInclude.Name));
            outputIncludes.emplace_back(modBuilder.AssumeResolved(outputInclude.Name));
        }

        outputIncludeForType.emplace(type, TNodeAddCtx::CreateParsedIncls(
            Module, *Graph, *UpdIter, UpdIter->YMake,
            mainOutType, mainOutId, type, outputIncludes
        ));
    });

    auto addOutputIncludes = [&](TAddDepAdaptor& addCtx) {
        for (const auto& [_, parsedIncludes]: outputIncludeForType) {
            if (auto* node = parsedIncludes.Node) {
                addCtx.AddDepIface(EDT_Property, node->NodeType, node->ElemId);
            }
        }
    };

    const bool hasExtraOuts = GetOutput().size() > startCountOuts;
    const bool mainOutAsExtra = hasExtraOuts && !IsModuleType(mainOutType);

    const bool addModuleNode = Conf->DedicatedModuleNode() && IsModuleType(mainOutType);
    EMakeNodeType moduleType = EMNT_Last;
    TAddDepAdaptor* moduleNode = nullptr;

    auto makeMainNodes = [&]() {
        EMakeNodeType fileNodeType = mainOutType;

        Y_ASSERT(!fileConf.GetName(mainOutId).IsLink());
        TStringBuf mainOutName = fileConf.GetName(mainOutId).GetTargetStr();

        if (addModuleNode) {
            Y_ASSERT(finalTargetCmd);
            fileNodeType = EMNT_NonParsedFile;
            moduleType = mainOutType;

            static constexpr TStringBuf modulePrefix = "$L/MODULE/"sv;
            TFileElemId moduleId = fileConf.Add(TString::Join(modulePrefix, mainOutName));
            moduleNode = &inputNode.AddOutput(moduleId, mainOutType);
        }

        if (mainOutAsExtra) {
            Y_ASSERT(IsFileType(fileNodeType));

            // Это пока очень временный способ пометить специальный узел, в котором будут общие свойства команды.
            static constexpr TStringBuf actionPrefix = "$L/ACTION/"sv;
            TFileElemId actionId = fileConf.Add(TString::Join(actionPrefix, mainOutName));

            TAddDepAdaptor& actionNode = inputNode.AddOutput(actionId, EMNT_NonParsedFile, !finalTargetCmd);
            TAddDepAdaptor& mainOutNode = inputNode.AddOutput(mainOutId, fileNodeType, !finalTargetCmd);

            return std::make_pair(std::ref(actionNode), std::ref(mainOutNode));
        } else {

            TAddDepAdaptor& mainOutNode = inputNode.AddOutput(mainOutId, fileNodeType, !finalTargetCmd);
            return std::make_pair(std::ref(mainOutNode), std::ref(mainOutNode));
        }
    };

    auto [actionNode, mainOutNode] = makeMainNodes();

    TVector<std::pair<std::reference_wrapper<TAddDepAdaptor>, TVarStrEx*>> outs;
    outs.push_back({mainOutNode, MainOutput});

    // 1. Inputs
    const TCmdElemId groupId = AssumeCmd(Graph->Names().AddName(EMNT_Property, NStaticConf::INPUTS_MARKER));
    for (auto& input : GetInput()) {
        YDIAG(DG) << "Input dep: " << input.Name << Endl;

        if (input.IsGlob) {
            if (modBuilder.CurrentInputGroup != groupId) {
                inputNode.AddDepIface(EDT_Group, EMNT_Property, groupId);
                modBuilder.CurrentInputGroup = groupId;
            }
            try {
                EncodeGlobInput(actionNode, globEvaluator.Evaluate(input.Name));
            } catch (const yexception& error) {
                globEvaluator.ReportInvalidPattern(input.Name, error.what());
            }
            continue;
        }

        if (finalTargetCmd) {
            AttachModuleInput(modBuilder, input, inputNode, groupId);
        } else {
            EMakeNodeType nodeType = EMNT_File;
            if (input.IsMacro) {
                nodeType = EMNT_UnknownCommand;
            } else if (input.IsDir) {
                nodeType = EMNT_MissingDir;
            } else if (input.NotFound) {
                nodeType = EMNT_MissingFile;
            } else if (input.IsOutputFile) {
                nodeType = EMNT_NonParsedFile;
            }

            if (nodeType == EMNT_NonParsedFile && TFileConf::IsLink(AssumeFile(input.ElemId))) {
                TFileElemId targetId = TFileConf::GetTargetId(AssumeFile(input.ElemId));
                auto inputNode = Graph->GetFileNodeById(targetId);
                if (inputNode.IsValid() && IsModuleType(inputNode->NodeType)) {
                    input.Name = NPath::GetTargetFromLink(input.Name);
                    input.ElemId = targetId;
                    nodeType = inputNode->NodeType;
                }
            }
            actionNode.AddDepIface(EDT_BuildFrom, nodeType, input.ElemId);
        }
        if (TFileConf::IsLink(AssumeFile(input.ElemId)) && NPath::GetType(NPath::ResolveLink(input.Name)) == NPath::ERoot::Build) {
            UpdIter->DelayedSearchDirDeps.GetDepsByType(EDT_Include)[MakeDepsCacheId(EMNT_NonParsedFile, input.ElemId)].Push(TFileConf::GetTargetId(AssumeFile(input.ElemId)));
        }
    }

    // 2. Additional output files (we have to add them after the inputs or Induced deps processing will fail)
    // NOTE: this was previously after BuildCommand!
    if (hasExtraOuts) {
        YDIAG(V) << "For " << inputNodeName << " deps.size = " << GetOutput().size() - startCountOuts << "\n";
        for (auto& out : GetOutput()) {
            if (out.ElemId == mainOutId) {
                continue;
            }

            TAddDepAdaptor& extraOutNode = inputNode.AddOutput(AssumeFile(out.ElemId), EMNT_NonParsedFile);
            outs.push_back({extraOutNode, &out});

            out.IsGlobal = out.IsGlobal || HasGlobalInput;
        }
    }

    {
        THashSet<TPropertyType> inducedDepsToUse;
        inducedDepsToUse.insert(TPropertyType{Graph->Names(), EVI_InducedDeps, "*"});
        bool mainOut = true;
        for (auto [outNodeRef, outVar] : outs) {
            TAddDepAdaptor& outNode = outNodeRef;

            if (mainOutAsExtra || !mainOut) {
                YDIAG(Star) << "Linking main " << actionNode.ElemId << " <-> " << outNode.ElemId << Endl;
                outNode.SetAction(&actionNode);
                outNode.AddDepIface(EDT_OutTogether, actionNode.NodeType, actionNode.ElemId);
                actionNode.AddDepIface(EDT_OutTogetherBack, outNode.NodeType, outNode.ElemId);
            }

            // Current implementation sets "pass induced" flags only for main output.
            // It is considered bug, but correct behaviour should be enabled only after additional testing.
            static constexpr bool oldPassMode = true;
            const bool setPassFlags = oldPassMode ? mainOut : true;

            // outVar is nullptr for "finalTargetCmd", which means this is a module target.
            // And we do not pass induced dependencies through modules.
            if (outVar) {
                const TIndDepsRule* rule = outNode.SetDepsRuleByName(outVar->Name);
                if (rule) {
                    rule->InsertUseActionsTo(inducedDepsToUse);
                }

                if (setPassFlags) {
                    auto setFlags = [&](TElemId elemId) {
                        TNodeData& nodeData = Graph->GetFileNodeData(elemId);
                        rule ? rule->ApplyNodeFlags(nodeData) : TIndDepsRule::ResetNodeFlags(nodeData);
                    };

                    setFlags(outNode.ElemId);

                    if (mainOut && mainOutAsExtra) {
                        setFlags(actionNode.ElemId);
                    }
                }
            }

            mainOut = false;
        }

        for (const auto& out : GetOutput()) {
            UpdIter->MainOutputId[out.ElemId] = mainOutId;
        }
        UpdIter->PropsToUse[mainOutId] = std::move(inducedDepsToUse);
    }

    if (moduleNode) {
        actionNode.AddDepIface(EDT_OutTogether, moduleNode->NodeType, moduleNode->ElemId);
        moduleNode->AddDepIface(EDT_OutTogetherBack, actionNode.NodeType, actionNode.ElemId);
    }

    if (finalTargetCmd) {
        for (const auto id : ownEntries) {
            Y_ENSURE(UpdIter != nullptr);
            auto modInfo = UpdIter->GetAddedModuleInfo(MakeDepFileCacheId(id));
            Y_ASSERT(modInfo != nullptr);
            if (modInfo != nullptr && modInfo->AdditionalOutput) {
                actionNode.AddDepIface(EDT_OutTogether, EMNT_NonParsedFile, id);
            }
        }
    }

    // OUTPUT_INCLUDES of main output must appear after OutTogetherBack edges
    for (auto [outNodeRef, _] : outs) {
        addOutputIncludes(outNodeRef);
    }

    // 4. The command
    YDIAG(DG) << "Cmd dep: " << curCmdName << " " << Cmd.Id << Endl;
    AttachAction(actionNode, cmdElemId);

    // 5. Imported variables
    if (LocalVars) {
        TVector<TStringBuf> names;
        names.reserve(LocalVars->size());
        for (auto& var : *LocalVars)
            names.push_back(var.first);
        Sort(names);
        for (auto name : names) {
            auto storedInProperties = name.ends_with("__LATEOUT__");
            if (storedInProperties)
                continue;
            auto& var = LocalVars->at(name);
            auto varElemId = InternCommand(
                var,
                EStorageFormat::Structured,
                EExpressionRole::Binding
            );
            AttachBinding(actionNode, varElemId, EBindingPlacement::Local);
        }
        LocalVars.Reset();
    }
    if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
        if (GlobalVars) {
            TVector<TStringBuf> names;
            names.reserve(GlobalVars->size());
            for (auto& var : *GlobalVars)
                names.push_back(var.first);
            Sort(names);
            for (auto name : names) {
                auto& var = GlobalVars->at(name);
                auto varElemId = InternCommand(
                    var,
                    EStorageFormat::Structured,
                    EExpressionRole::Binding
                );
                AttachBinding(actionNode, varElemId, EBindingPlacement::GlobalCompatibility);
            }
            GlobalVars.Reset();
        }
    }

    return EActionEncodingResult::NeedsCompletion;
}

void TActionGraphEncoder::CompleteActionSubmission(
    TAutoPtr<TCommandInfo>& commandInfoOwner,
    TModuleBuilder& modBuilder,
    TPreparedSubmission& submission,
    bool finalTargetCmd
) const {
    TAddDepAdaptor& inputNode = *submission.Storage_;
    auto& commandInfo = *commandInfoOwner;

    if (finalTargetCmd) {
        inputNode.AddOutput(AssumeFile(inputNode.ElemId), EMNT_NonParsedFile, false)
            .GetAction()
            .GetModuleData()
            .CmdInfo = commandInfoOwner;
    } else if (const auto* mainOutput = commandInfo.GetMainOutput()) {
        inputNode.AddOutput(AssumeFile(mainOutput->ElemId), EMNT_NonParsedFile, false)
            .GetAction()
            .GetModuleData()
            .CmdInfo = commandInfoOwner;
    }

    if (!finalTargetCmd) {
        modBuilder.QueueCommandOutputs(commandInfo);
    }
}

void TActionGraphEncoder::EncodeGlobInput(TAddDepAdaptor& node, TEvaluatedActionGlob&& glob) const {
    TVector<TFileElemId> matchedFiles;
    matchedFiles.reserve(glob.MatchedPaths.size());
    for (const auto path : glob.MatchedPaths) {
        matchedFiles.push_back(
            Graph_.Names().FileConf.ConstructLink(
                ELinkType::ELT_Text,
                Graph_.Names().FileConf.GetName(path)
            ).GetElemId()
        );
    }

    const TString globCommand = FormatCmd(
        Module_->GetName().GetElemId(),
        NProps::LATE_GLOB,
        glob.Pattern
    );
    TModuleGlobInfo globInfo = {
        .GlobPatternId = AssumeCmd(Graph_.Names().AddName(EMNT_BuildCommand, globCommand)),
        .GlobPatternHash = AssumeCmd(Graph_.Names().AddName(
            EMNT_Property,
            FormatProperty(NProps::GLOB_HASH, glob.MatchesHash)
        )),
        .WatchedDirs = std::move(glob.WatchedDirectories),
        .MatchedFiles = std::move(matchedFiles),
        .Excludes = {},
        .ReferencedByVar = TCmdElemId(),
    };

    const auto globPatternId = globInfo.GlobPatternId;
    const auto nodeType = EMNT_BuildCommand;
    node.AddUniqueDep(EDT_BuildFrom, nodeType, globPatternId);
    auto& [id, entryStats] = *UpdIter_.Nodes.Insert(
        MakeDepsCacheId(nodeType, globPatternId),
        &UpdIter_.YMake,
        Module_
    );
    auto& globNode = entryStats.GetAddCtx(Module_, UpdIter_.YMake);
    globNode.NodeType = nodeType;
    globNode.ElemId = globPatternId;
    entryStats.SetOnceEntered(false);
    entryStats.SetReassemble(true);
    PopulateGlobNode(globNode, globInfo);
}

bool TActionGraphEncoder::CompleteVariableSubmission(
    TCommandInfo& commandInfo,
    TPreparedSubmission& submission
) const {
    TAddDepAdaptor& inputNode = *submission.Storage_;
    auto& Cmd = commandInfo.Cmd;
    auto GetInput = [&]() { return commandInfo.GetInput(); };

    YDIAG(SUBST) << "Process command: " << Get1(&Cmd) << Endl;

    if (!Cmd) {
        return true;
    }

    // 1. Inputs
    for (auto& input : GetInput()) {
        AttachVariableInput(input, inputNode);

        if (TFileConf::IsLink(AssumeFile(input.ElemId)) && NPath::GetType(NPath::ResolveLink(input.Name)) == NPath::ERoot::Build) {
            UpdIter_.DelayedSearchDirDeps.GetDepsByType(EDT_Include)[MakeDepsCacheId(EMNT_NonParsedFile, input.ElemId)].Push(TFileConf::GetTargetId(AssumeFile(input.ElemId)));
        }
    }

    return true;
}

TVector<TStringBuf> TActionGraphEncoder::ConfigurationBindingVariables(
    const TVector<TDepsCacheId>& varLists
) const {
    TVector<TStringBuf> result;
    for (auto id : varLists) { // this loop is not optimized because there's hardly 1 element in varLists
        TStringBuf name = Graph_.GetCmdNameByCacheId(id).GetStr();
        TVector<TStringBuf> vars;
        Split(GetPropertyValue(name), " ", vars);
        result.insert(result.end(), vars.begin(), vars.end());
    }
    return result;
}

void TActionGraphEncoder::AddConfigurationBinding(
    TCompiledBindingExpression&& binding,
    TNodeAddCtx& dst
) const {
    // Attach CFG_VARS to the source file.
    const auto importedExpression = ImportCommandExpression(std::move(binding.Expression));
    auto subBinding = TYVar();
    subBinding.SetSingleVal("CFG_VARS", importedExpression.Reference, TElemId());
    auto cmdElemId = InternCommand(
        subBinding,
        EStorageFormat::Structured,
        EExpressionRole::Binding
    );
    dst.AddDep(EDT_BuildCommand, EMNT_BuildVariable, cmdElemId);
}

void TActionGraphEncoder::AttachModuleInput(
    TModuleBuilder& moduleBuilder,
    TVarStrEx& input,
    TAddDepAdaptor& submission,
    TElemId groupId
) const {
    YDIAG(DG) << "SRCS dep for module: " << input.Name << " " << input.ElemId << Endl;
    Y_ENSURE(input.ElemId);
    Y_ENSURE(input.IsMacro || input.IsPathResolved);
    const EMakeNodeType nodeType = ActionInputNodeType(input);

    const bool builtInThisModule = !input.IsGlobal || !moduleBuilder.GetModuleConf().Globals.contains("SRCS");
    moduleBuilder.HasBuildFrom |= builtInThisModule;
    TElemId* currentGroupId = &moduleBuilder.CurrentInputGroup;
    if (submission.ElemId == moduleBuilder.GlobalNodeElemId) {
        currentGroupId = &moduleBuilder.CurrentGlobalInputGroup;
    } else {
        Y_ASSERT(submission.ElemId == moduleBuilder.ModuleNodeElemId);
    }

    if (*currentGroupId != groupId) {
        submission.AddDepIface(EDT_Group, EMNT_Property, groupId);
        *currentGroupId = groupId;
    }
    if (builtInThisModule) {
        submission.AddDepIface(EDT_BuildFrom, nodeType, input.ElemId);
    }
    if (input.IsGlobal) {
        if (Module_->GetAttrs().UseGlobalCmd && moduleBuilder.ModuleDef->IsGlobalInput(input.Name)) {
            Y_ASSERT(submission.ElemId == moduleBuilder.GlobalNodeElemId);
            if (*currentGroupId != groupId) {
                submission.AddDepIface(EDT_Group, EMNT_Property, groupId);
                *currentGroupId = groupId;
            }
            submission.AddDepIface(EDT_BuildFrom, nodeType, input.ElemId);
            moduleBuilder.GlobalSrcsAreAdded = true;
        } else {
            submission.AddUniqueDep(EDT_Search2, nodeType, input.ElemId);
        }
    }
}

void TActionGraphEncoder::AttachVariableInput(
    TVarStrEx& input,
    TAddDepAdaptor& submission
) const {
    Y_ENSURE(input.ElemId);
    Y_ENSURE(input.IsMacro || input.IsPathResolved);
    const EMakeNodeType nodeType = ActionInputNodeType(input);
    submission.AddDepIface(EDT_BuildFrom, nodeType, input.ElemId);
}

void TActionGraphEncoder::RecordGlobalBindingUse(TStringBuf varName) const {
    // enforce the existence of an elemId for a USED_RESERVED_VAR prop node
    // to be used by the cache, see TSaveBuffer::FindUsedReservedVar
    Graph_.Names().AddName(EMNT_BuildCommand, TString::Join(NProps::USED_RESERVED_VAR, "=", varName));
}

void TActionGraphEncoder::AttachGlobalBinding(
    TCompiledGlobalBinding&& binding,
    TPreparedSubmission& submission
) const {
    // TODO: there's no point in allocating cmdElemId for expressions
    // that do _not_ have directly corresponding nodes
    // (and are linked as "0:VARNAME=S:123" instead)
    const auto importedExpression = ImportCommandExpression(std::move(binding.Value.Expression));
    const auto compiledVarText = FormatCmd(
        TCmdElemId(binding.CommandId),
        binding.CommandName,
        importedExpression.Reference
    );
    const auto compiledVarElemId = AssumeCmd(Graph_.Names().AddName(EMNT_BuildCommand, compiledVarText));

    TCommandInfo cmdInfo(Conf_, &Graph_, &UpdIter_, Module_);
    cmdInfo.GetCommandInfoFromStructVar(
        compiledVarElemId,
        importedExpression.Id,
        UpdIter_.YMake.Commands,
        Conf_.CommandConf
    );

    TAddDepAdaptor& node = *submission.Storage_;
    if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
        // duplication comes from adding locally referenced vars
        // via TCommandInfo::GlobalVars, then the whole list through here
        node.AddUniqueDep(EDT_Include, EMNT_BuildCommand, compiledVarElemId);
    } else {
        node.AddDepIface(EDT_Include, EMNT_BuildCommand, compiledVarElemId);
    }
}
