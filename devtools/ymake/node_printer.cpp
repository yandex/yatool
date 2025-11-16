#include "node_printer.h"

#include "ymake.h"
#include "module_restorer.h"
#include "macro.h"
#include "mkcmd.h"
#include "shell_subst.h"
#include "flat_json_graph.h"
#include "tools_miner.h"
#include "isolated_projects.h"
#include "command_store.h"

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/loops.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/managed_deps_iter.h>
#include <devtools/ymake/resolver/path_resolver.h>
#include <devtools/ymake/resolver/resolve_ctx.h>
#include <devtools/ymake/vars.h>

#include <library/cpp/containers/top_keeper/top_keeper.h>
#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_writer.h>
#include <library/cpp/string_utils/levenshtein_diff/levenshtein_diff.h>

#include <util/folder/path.h>
#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/memory/pool.h>
#include <util/stream/output.h>
#include <util/stream/str.h>
#include <util/string/escape.h>
#include <util/string/join.h>
#include <util/string/type.h>
#include <util/system/types.h>

#include <algorithm>
#include <fmt/format.h>
#include <utility>

namespace {

    int PrepareSkipFlags(const TDebugOptions& conf) {
        return
            (conf.SkipRecurses || conf.SkipAllRecurses ? TDependencyFilter::SkipRecurses : 0)
            | (conf.SkipDepends ? TDependencyFilter::SkipDepends : 0)
            | (conf.SkipTools ? TDependencyFilter::SkipTools : 0)
            | (conf.SkipAddincls ? TDependencyFilter::SkipAddincls : 0)
            // We prefer direct Module->Module peerdirs as precise
            // and ignore old-style Module->Dir->Module peerdirs
            | (conf.DumpIndirectPeerdirs ? 0 : TDependencyFilter::SkipModules | TDependencyFilter::SkipRecurses);
    }

    int PrepareSubGraphSkipFlags(const TDebugOptions& conf) {
        return conf.DumpAddinclsSubgraphs ? 0 : TDependencyFilter::SkipAddincls;
    }

    template<typename TFunc>
    void IterateIndirectSrcs(const TConstDepRef& dep, TFunc func) {
        Y_ASSERT(IsIndirectSrcDep(dep));
        for (const auto& edge: dep.To().Edges()) {
            if (*edge == EDT_Property && edge.To()->NodeType == EMNT_File) {
                func(edge.To());
            }
        }
    }

    TString FormatBuildCommandName(const TCmdView& name, const TCommands& commands, const TCmdConf& cmdConf) {
        if (name.IsNewFormat()) {
            auto expr = commands.Get(commands.IdByElemId(name.GetElemId()));
            Y_ASSERT(expr);
            return commands.PrintCmd(*expr);
        } else {
            ui64 varId;
            TStringBuf varName;
            TStringBuf varValue;
            ParseCommandLikeVariable(name.GetStr(), varId, varName, varValue);
            auto expr = commands.Get(varValue, &cmdConf);
            Y_ASSERT(expr);
            return FormatCmd(varId, varName, commands.PrintCmd(*expr));
        }
    }

    void PatchStrings(const std::invocable<const TString&> auto f, NJson::TJsonValue& x) {
        if (x.IsMap())
            for (auto& [k, v] : x.GetMapSafe())
                PatchStrings(f, v);
        else if (x.IsArray())
            for (auto& v : x.GetArraySafe())
                PatchStrings(f, v);
        else if (x.IsString())
            x = f(x.GetString());
    }
}

class TFormatInterface {
public:
    virtual void AddHeader() {}
    virtual void AddTail() {}

    virtual void BeginNode() {}
    virtual void EndNode() {}

    virtual void BeginChildren() {}
    virtual void EndChildren() {}

    virtual void EmitDep(const ui32 /*fromId*/, const EMakeNodeType /*fromType*/, const ui32 /*toId*/, const EMakeNodeType /*toType*/, const EDepType /*depType*/, bool /*isFirst*/, const ELogicalDepType /*logicalDepType*/ = ELDT_FromDepType) {}

    virtual void EmitName(const TStringBuf& /*parentName*/, const TStringBuf& /*name*/) {}

    virtual void EmitNode(const EMakeNodeType /*type*/, const ui32 /*id*/, const TStringBuf& /*parentName*/, const TStringBuf& /*name*/, const TStringBuf& /*flags*/) {}

    virtual void EmitPad(const TStringBuf& /*pad*/) {}

    virtual void EmitModule(EMakeNodeType /*type*/, const TStringBuf& /*moduleName*/, const TStringBuf& /*name*/) {}

    virtual void EmitPos(int /*id*/, int /*of*/) {}
};

class TJsonFormat : public TFormatInterface {
private:
    NJsonWriter::TBuf Writer;

    void EmitId(int id) {
        Writer.WriteKey("id");
        Writer.WriteInt(id);
    }

public:
    static const bool NeedParentName = false;
    static const bool PrintChildren = true; // for sorted output only
    TJsonFormat(IOutputStream* stream)
        : Writer(NJsonWriter::HEM_DONT_ESCAPE_HTML, stream)
    {
        Writer.SetIndentSpaces(2);
    }

    void AddHeader() override {
        Writer.BeginObject();
        Writer.WriteKey("graph");
        Writer.BeginList();
    }

    void AddTail() override {
        Writer.EndList();
        Writer.EndObject();
    }

    void BeginNode() override {
        Writer.BeginObject();
    }

    void EndNode() override {
        Writer.EndObject();
    }

    void BeginChildren() override {
        Writer.WriteKey("deps");
        Writer.BeginList();
    }

    void EndChildren() override {
        Writer.EndList();
    }

    void EmitName(const TStringBuf& /*parentName*/, const TStringBuf& name) override {
        Writer.WriteKey("name");
        Writer.WriteString(name);
    }

    void EmitNode(const EMakeNodeType type, const ui32 id, const TStringBuf& parentName, const TStringBuf& name, const TStringBuf&) override {
        Writer.WriteKey("node-type");
        Writer.WriteString(TStringBuilder() << type);
        EmitId(id);
        EmitName(parentName, name);
        BeginChildren();
    }

    void EmitDep(const ui32, const EMakeNodeType, const ui32, const EMakeNodeType, const EDepType depType, bool isFirst, const ELogicalDepType logicalDepType = ELDT_FromDepType) override {
        Writer.WriteKey("dep-type");
        if (logicalDepType == ELDT_FromDepType) {
            Writer.WriteString(TStringBuilder() << depType);
        } else {
            Writer.WriteString(TStringBuilder() << logicalDepType);
        }
        Writer.WriteKey("is-first");
        Writer.WriteBool(isFirst);
    }

    // flat output
    void EmitModule(EMakeNodeType /*type*/, const TStringBuf& /*modName*/, const TStringBuf& /*name*/) override {
        ythrow TNotImplemented() << "TJsonFormat::EmitModule is not available yet";
    }
};

class THumanReadableFormat : public TFormatInterface {
private:
    IOutputStream* Stream_;

    void EmitNodeType(const EMakeNodeType nodeType) {
        *Stream_ << "Type: " << nodeType << ", ";
    }

    void EmitId(int id) {
        *Stream_ << "Id: " << id << ", ";
    }

    void EmitFlags(const TStringBuf& flags) {
        if (!flags.empty())
            *Stream_ << "Flags: [" << flags << "], ";
    }
public:
    static const bool NeedParentName = false;
    static const bool PrintChildren = true; // for sorted output only
    THumanReadableFormat(IOutputStream* stream)
        : Stream_(stream)
    {
    }

    void EmitPad(const TStringBuf& pad) override {
        *Stream_ << pad;
    }

    void EmitPos(int id, int of) override {
        *Stream_ << "[" << id << " of " << of << "] ";
    }

    void EmitName(const TStringBuf& /*parentName*/, const TStringBuf& name) override {
        *Stream_ << "Name: " << name;
    }

    void EmitDep(const ui32 /*fromId*/, const EMakeNodeType /*fromType*/, const ui32 /*toId*/, const EMakeNodeType /*toType*/, const EDepType depType, bool isFirst, const ELogicalDepType logicalDepType = ELDT_FromDepType) override {
        if (logicalDepType == ELDT_FromDepType) {
            *Stream_ << "Dep: " << depType << (isFirst ? "" : "*") << ", ";
        } else {
            *Stream_ << "Dep: " << logicalDepType << (isFirst ? "" : "*") << ", ";
        }
    }

    void EmitNode(const EMakeNodeType type, const ui32 id, const TStringBuf& parentName, const TStringBuf& name, const TStringBuf& flags) override {
        EmitNodeType(type);
        EmitId(id);
        EmitFlags(flags);
        EmitName(parentName, name);
        BeginChildren();
    }

    void BeginChildren() override {
        *Stream_ << Endl;
    }

    // flat output
    void EmitModule(EMakeNodeType nodeType, const TStringBuf& modName, const TStringBuf& name) override {
        *Stream_ << "module: " << nodeType << " " << modName << " " << name << Endl;
    }
};

template <bool PrintDeps>
class TDotFormat : public TFormatInterface {
private:
    IOutputStream* Stream_;

public:
    static const bool NeedParentName = PrintDeps;
    static const bool PrintChildren = PrintDeps; // for sorted output only
    TDotFormat(IOutputStream* stream)
        : Stream_(stream)
    {}

    void BeginChildren() override {
        *Stream_ << Endl;
    }

    void EmitPad(const TStringBuf&) override {
        *Stream_ << "    ";
    }

    void EmitName(const TStringBuf& parentName, const TStringBuf& name) override {
        // TODO: quote symbols
        if (!NeedParentName)
            *Stream_ << "\"" << EscapeC(name) << "\";";
        else if (parentName.size())
            *Stream_ << "\"" << EscapeC(parentName) << "\" -> \"" << EscapeC(name) << "\";";
    }

    void EmitNode(const EMakeNodeType /*type*/, const ui32 /*id*/, const TStringBuf& parentName, const TStringBuf& name, const TStringBuf&) override {
        EmitName(parentName, name);
        BeginChildren();
    }

    // flat output
    void EmitModule(EMakeNodeType /*type*/, const TStringBuf& /*modName*/, const TStringBuf& name) override {
        *Stream_ << "    \"" << EscapeC(NPath::CutType(name)) << "\" [label=\"" << EscapeC(NPath::Parent(NPath::CutType(name))) << "\"];" << Endl;
    }
};

class TFlatJsonFormat : public TFormatInterface, public NFlatJsonGraph::TWriter {
public:
    static const bool NeedParentName = true;
    static const bool PrintChildren = false;

    TFlatJsonFormat(IOutputStream* stream)
    : NFlatJsonGraph::TWriter(*stream) {}

    void EmitDep(const ui32 fromId, const EMakeNodeType fromType, const ui32 toId, const EMakeNodeType toType, const EDepType  depType, bool /*isFirst*/, const ELogicalDepType logicalDepType = ELDT_FromDepType) override {
        AddLink(fromId, fromType, toId, toType, depType, NFlatJsonGraph::EIDFormat::Complex, logicalDepType);
    }

    void EmitNode(const EMakeNodeType type, const ui32 id, const TStringBuf& /*parentName*/, const TStringBuf& name, const TStringBuf&) override {
        AddNode(type, id, name, NFlatJsonGraph::EIDFormat::Complex);
    }
};

template <class TFormatter>
class TNodePrinter: public TNoReentryStatsVisitor<TDumpEntrySt, TDepthDumpStateItem> {
private:
    using TBase = TNoReentryStatsVisitor<TDumpEntrySt, TDepthDumpStateItem>;

    const TSymbols& Names;
    const TRestoreContext RestoreContext;
    IOutputStream& Cmsg;
    const THashSet<TTarget>& StartTargets;
    TString Pad;
    TFormatter Formatter_;
    TDependencyFilter DepFilter;
    TDependencyFilter SubGraphFilter;
    TToolMiner ToolMiner;
    const TCommands& Commands;
    bool DumpDepends = false;

    const TFoldersTree* FoldersTree;
    THashSet<ui32> SeenDataPaths;
    THashMap<ui32, TNodeId> DirElemId2ModuleNodeId;
    bool WithData = false;
    bool WithYaMake = false;
    bool SkipMakeFiles = false;
    bool MarkMakeFiles = false;
    bool NeedResolveLinks = false;

public:
    using TNodes = typename TBase::TNodes; // MSVC2015 is not happy with just `using typename TBase::TNodes`
    using TBase::Nodes;
    THashMap<TNodeId, TNodeId> Node2Module;

    enum EDumpMode {
        DM_MakeFiles,
        DM_Files,
        DM_Dirs,
        DM_Modules,
        DM_SrcDeps,
        DM_Draph,
        DM_DraphNoId,
        DM_DraphNoPosNoId,
        DM_DGraphFlatJson,
        DM_DGraphFlatJsonWithCmds,
        DM_NoDump,
    };
    EDumpMode Mode; // TODO: use fieldcalc instead

    TNodePrinter(TRestoreContext restoreContext, const TSymbols& names,
                 const THashSet<TTarget>& startTargets, IOutputStream& cmsg,
                 const TDebugOptions& cf, const TCommands& commands,
                 const TFoldersTree* foldersTree = nullptr, const THashSet<ui32>* usedPaths = nullptr)
        : Names(names)
        , RestoreContext{restoreContext}
        , Cmsg(cmsg)
        , StartTargets(startTargets)
        , Formatter_(&cmsg)
        , DepFilter(PrepareSkipFlags(cf))
        , SubGraphFilter(PrepareSubGraphSkipFlags(cf))
        , Commands(commands)
        , DumpDepends(cf.DumpDepends)
        , FoldersTree(foldersTree)
        , WithData(cf.DumpData)
        , WithYaMake(cf.WithYaMake)
        , SkipMakeFiles(cf.SkipMakeFilesInDumps)
        , MarkMakeFiles(cf.MarkMakeFilesInDumps)
        , Mode(cf.DumpMakefiles ? DM_MakeFiles :
               cf.DumpFiles ? DM_Files :
               cf.DumpDirs ? DM_Dirs :
               cf.DumpModules ? DM_Modules :
               cf.DumpSrcDeps ? DM_SrcDeps :
               cf.DumpGraphFlatJsonWithCmds ? DM_DGraphFlatJsonWithCmds :
               cf.DumpGraphFlatJson ? DM_DGraphFlatJson :
               cf.DumpGraphNoPosNoId ? DM_DraphNoPosNoId :
               cf.DumpGraphNoId ? DM_DraphNoId :
               cf.DumpGraph ? DM_Draph : DM_NoDump
        )
    {
        if (usedPaths) {
            SeenDataPaths = *usedPaths;
        }

        auto& conf = RestoreContext.Conf.CommandConf;
        NeedResolveLinks = EqualToOneOf(Mode, DM_Files, DM_SrcDeps) && !IsFalse(conf.EvalValue("DUMP_GRAPH_RESOLVE_LINKS"sv));
    }

    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);

    TFormatter& Formatter() {
        return Formatter_;
    }
    const TFormatter& Formatter() const {
        return Formatter_;
    }
};

template <class TFormatter>
bool TNodePrinter<TFormatter>::AcceptDep(TState& state) {
    TBase::AcceptDep(state);
    const auto& dep = state.NextDep();
    bool isStartModuleDep = !state.HasIncomingDep() && IsDirToModuleDep(dep);
    if (isStartModuleDep && !StartTargets.contains(dep.To().Id())) {
        return false;
    }
    if (SkipMakeFiles && IsDirType(dep.From()->NodeType) && IsMakeFileType(dep.To()->NodeType)) {
        return false;
    }
    if (IsPeerdirDep(dep)) {
        const auto* module = RestoreContext.Modules.Get(dep.From()->ElemId);
        if (module->GetAttrs().RequireDepManagement) {
            const auto it = DirElemId2ModuleNodeId.find(dep.To()->ElemId);
            if (it.IsEnd() || !RestoreContext.Modules.GetModuleNodeLists(module->GetId()).UniqPeers().has(it->second)) {
                return false;
            }
        }
    }
    return DepFilter(dep, !state.HasIncomingDep()) && IsReachableManagedDependency(RestoreContext, dep);
}

template <class TFormatter>
bool TNodePrinter<TFormatter>::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    auto& top = state.Top();
    const auto topNode = top.Node();
    const auto nodeType = topNode->NodeType;
    const auto elemId = topNode->ElemId;

    if (fresh && IsModuleType(nodeType)) {
        const TModule* module = RestoreContext.Modules.Get(elemId);
        if (module->GetAttrs().RequireDepManagement) {
            for (TNodeId peerId: RestoreContext.Modules.GetModuleNodeLists(module->GetId()).UniqPeers()) {
                const TModule* peer = RestoreContext.Modules.Get(RestoreContext.Graph.Get(peerId)->ElemId);
                DirElemId2ModuleNodeId[peer->GetDirId()] = peerId;
            }
        }
    }

    auto hasIncDep = state.HasIncomingDep();
    const auto incDep = state.IncomingDep();
    if (fresh && hasIncDep && !SubGraphFilter(incDep)) {
        fresh = false;
        CurEnt->RejectedByIncomingDep = true;
    } else if (CurEnt && CurEnt->RejectedByIncomingDep && (!hasIncDep || SubGraphFilter(incDep))) {
        fresh = true;
        CurEnt->RejectedByIncomingDep = false;
    }

    if (!fresh) {
        top.Depth = CurEnt->Depth;
    }
    Formatter().BeginNode();

    auto resolveNodeName = [] (auto& graph, auto node, bool resolveLinks) {
        if (UseFileId(node->NodeType)) {
            if (resolveLinks) {
                return TString{graph.GetFileName(node).GetTargetStr()};
            } else {
                TString name;
                graph.GetFileName(node).GetStr(name);
                return name;
            }
        } else {
            return TString{graph.GetCmdName(node).GetStr()};
        }
    };

    TString name = resolveNodeName(RestoreContext.Graph, topNode, NeedResolveLinks);

    TString parentName;
    ui32 parentId = 0;
    EMakeNodeType parentType = EMakeNodeType::EMNT_Deleted;
    auto parent = state.Parent();
    if (parent != state.end()) {
        if (TFormatter::NeedParentName) {
            parentName = resolveNodeName(RestoreContext.Graph, parent->Node(), NeedResolveLinks);
            parentId = parent->Node()->ElemId;
            parentType = parent->Node()->NodeType;
        }
    }
    if (Pad.size() < state.Size()) {
        Pad.append(state.Size() - Pad.size(), ' ');
    }
    TStringStream ss;
    auto maybeEmitStructCmd = [&]() {
        auto isStructCommand = incDep.IsValid() && (*incDep == EDT_Include || *incDep == EDT_BuildCommand) && nodeType == EMNT_BuildCommand;
        auto isStructVariable = incDep.IsValid() && *incDep == EDT_BuildCommand && nodeType == EMNT_BuildVariable;
        if (isStructCommand || isStructVariable) {
            Formatter().EmitNode(
                nodeType,
                Names.CmdNameById(elemId).GetCmdId(),
                parentName,
                FormatBuildCommandName(top.GetCmdName(), Commands, Names.CommandConf),
                DumpNodeFlags(elemId, nodeType, Names)
            );
            return true;
        }
        return false;
    };
    switch (Mode) {
        case DM_MakeFiles: {
            bool isIncFile = hasIncDep && IsMakeFileIncludeDep(incDep.From()->NodeType, *incDep, incDep.To()->NodeType);
            if (fresh && (nodeType == EMNT_MakeFile || isIncFile)) {
                Cmsg << NPath::CutType(NPath::ResolveLink(name)) << Endl;
            }
            break;
        }
        case DM_Files:
            if (IsFileType(nodeType)) {
                bool isIncMakeFile = hasIncDep && IsMakeFileIncludeDep(incDep.From()->NodeType, *incDep, incDep.To()->NodeType);
                bool printAsMakeFile = MarkMakeFiles && (isIncMakeFile || nodeType == EMNT_MakeFile) && !CurEnt->WasPrintedAsMakeFile;
                bool printAsFile = fresh || MarkMakeFiles && !isIncMakeFile && !CurEnt->WasPrintedAsFile;
                if (printAsMakeFile) {
                    Cmsg << "makefile: " << (EMakeNodeType)nodeType << ' ' << name << Endl;
                    CurEnt->WasPrintedAsMakeFile = true;
                } else if (printAsFile) {
                    Cmsg << "file: " << (EMakeNodeType)nodeType << ' ' << name << Endl;
                    if (WithData && IsModuleType(nodeType)) {
                        auto mod = RestoreContext.Modules.Get(elemId);
                        Y_ASSERT(mod);
                        if (mod->DataPaths) {
                            for(auto dataPath: *mod->DataPaths) {
                                 if (SeenDataPaths.contains(dataPath.GetElemId())) {
                                     continue;
                                 }
                                 Cmsg << "data: ";

                                 if (!RestoreContext.Graph.Names().FileConf.YPathExists(dataPath)) {
                                      Cmsg << "Missing";
                                 } else if (RestoreContext.Graph.Names().FileConf.CheckDirectory(dataPath)) {
                                      Cmsg << "Directory";
                                 } else {
                                      Cmsg << "File";
                                 }
                                 Cmsg << " " << dataPath.GetTargetStr() << Endl;
                                 SeenDataPaths.insert(dataPath.GetElemId());
                            }
                        }
                    }
                    CurEnt->WasPrintedAsFile = true;
                }
            }
            break;
        case DM_Dirs:
            if (fresh && IsDirType(nodeType)) {
                Cmsg << "dir: " << (EMakeNodeType)nodeType << ' ' << name << Endl;
            }
            break;
        case DM_Modules:
            if (TFormatter::NeedParentName) {
                if (IsModule(top) && hasIncDep) {
                    if (IsDirectPeerdirDep(incDep)) {
                        TFileView pModName = state.Parent()->GetFileName();
                        Formatter().EmitPad(TStringBuf());
                        Formatter().EmitName(pModName.CutType(), NPath::CutType(name));
                        Formatter().BeginChildren();
                    }
                }
            } else {
                if (fresh && IsModule(top)) {
                    TStringBuf modname = NPath::CutType(NPath::Parent(name));
                    Formatter().EmitModule(nodeType, modname, name);
                }
            }
            break;
        case DM_SrcDeps:
            if (fresh) {
                if (IsMakeFileType(nodeType) && WithYaMake) {
                    Cmsg << "Makefile: " << name << Endl;
                } else if (nodeType == EMNT_File) {
                    if(SeenDataPaths.contains(top.GetFileName().GetElemId())) {
                        break;
                    }
                    Y_ASSERT(FoldersTree != nullptr);
                    if (!FoldersTree->ExistsParentPathOf(name)) {
                        Cmsg << "File: " << name << Endl;
                    }
                }
            }
            break;
        case DM_DGraphFlatJsonWithCmds:
        case DM_DGraphFlatJson:
            if (Mode == DM_DGraphFlatJsonWithCmds || UseFileId(parentType) && UseFileId(nodeType)) {
                Formatter().EmitDep(parentId, parentType, elemId, nodeType, incDep.IsValid() ? *incDep : EDT_Search, fresh);
            }
            if (fresh && (Mode == DM_DGraphFlatJsonWithCmds || UseFileId(nodeType))) {
                if (!maybeEmitStructCmd()) {
                    Formatter().EmitNode(
                        nodeType,
                        elemId,
                        parentName,
                        (nodeType == EMNT_BuildCommand ? SkipId(name) : name),
                        DumpNodeFlags(elemId, nodeType, Names)
                    );
                }
            }
            if (Mode == DM_DGraphFlatJson && IsOutputType(nodeType)) {
                const auto tools = ToolMiner.MineTools(state.TopNode());
                for (ui32 toolId : tools) {
                    Formatter().EmitDep(elemId, nodeType, toolId, TDepGraph::Graph(topNode).GetFileNodeById(toolId)->NodeType, EDepType::EDT_Include, fresh);
                }
            }
            if (DumpDepends && hasIncDep && parentType == EMNT_BuildCommand && parent != state.end() && IsPropToDirSearchDep(incDep) && parentName.EndsWith("DEPENDS=")) {
                // emit deps to modules closure of depend
                auto propParent = parent + 1;
                if (propParent != state.end()) {
                    for (const auto topEdge: topNode.Edges()) {
                        if (IsDirToModuleDep(topEdge)) {
                            const auto modRef = topEdge.To();
                            const TModule* mod = RestoreContext.Modules.Get(modRef->ElemId);
                            if (mod && !mod->IsFakeModule() && (!mod->IsFromMultimodule() || mod->IsFinalTarget())) {
                                Formatter().EmitDep(propParent->Node()->ElemId, propParent->Node()->NodeType, modRef->ElemId, modRef->NodeType, EDT_Include, fresh, ELDT_Depend);
                            }
                        }
                    }
                }
            }
            break;
        case DM_DraphNoPosNoId:
        case DM_DraphNoId:
        case DM_Draph:
            Formatter().EmitPad(TStringBuf(Pad).Trunc(state.Size()));
            if (Mode != DM_DraphNoPosNoId) {
                Formatter().EmitPos(incDep.IsValid() ? parent->DepIndex() + 1 : 0, incDep.IsValid() ? parent->NumDeps() : 0);
            }
            Formatter().EmitDep(parentId, parentType, elemId, nodeType, incDep.IsValid() ? *incDep : EDT_Search, fresh);
            {
                if (!maybeEmitStructCmd()) {
                    Formatter().EmitNode(
                        nodeType,
                        UseFileId(nodeType) ? TDepGraph::GetFileName(topNode).GetTargetId() : elemId,
                        parentName,
                        (Mode == DM_Draph || nodeType != EMNT_BuildCommand ? name : SkipId(name)),
                        DumpNodeFlags(elemId, nodeType, Names)
                    );
                }
            }
            break;

        case DM_NoDump:
            break;
    }

    return fresh;
}

template <class TFormatter>
void TNodePrinter<TFormatter>::Leave(TState& state) {
    TBase::Leave(state);
    TStateItem& st = state.Top();
    CurEnt->Depth = st.Depth;
    if (CurEnt->HasBuildFrom) {
        auto modIt = FindModule(state);
        TNodeId& n2m = Node2Module[st.Node().Id()];
        if (n2m == TNodeId::Invalid) {
            n2m = modIt != state.end() ? modIt->Node().Id() : TNodeId::Invalid;
        }
    }
}

template <class TFormatter>
void TNodePrinter<TFormatter>::Left(TState& state) {
    TDumpEntrySt* childEnt = CurEnt;
    TBase::Left(state);
    state.Top().CheckDepth(childEnt->Depth);
    Formatter().EndChildren();
    Formatter().EndNode();
}

template <class TFormatter>
static void DumpGraphInternal(TYMake& yMake) {
    TNodePrinter<TFormatter> printer(yMake.GetRestoreContext(), yMake.Names, yMake.ModuleStartTargets, yMake.Conf.Cmsg(), yMake.Conf, yMake.Commands);
    printer.Formatter().AddHeader();

    {
        THashSet<TNodeId> usedStartTargets;
        for (const auto& startTarget : yMake.StartTargets) {
            if (startTarget.IsModuleTarget) {
                continue;
            }
            if (usedStartTargets.contains(startTarget.Id)) {
                continue;
            }
            if (yMake.Conf.SkipAllRecurses && !startTarget.IsUserTarget) {
                continue;
            }
            if (startTarget.IsNonDirTarget) {
                continue;
            }
            if (startTarget.IsDependsTarget && !startTarget.IsRecurseTarget && yMake.Conf.SkipDepends) {
                continue;
            }
            IterateAll(yMake.Graph, startTarget, printer);
            printer.Formatter().EndChildren();
            printer.Formatter().EndNode();
            usedStartTargets.insert(startTarget.Id);
        }
    }

    printer.Formatter().AddTail();

    if (yMake.Conf.DumpPretty)
        yMake.Conf.Cmsg() << Endl;

    bool renderCmd = yMake.Conf.DumpRenderedCmds;
    bool listBuildables = yMake.Conf.DumpBuildables;
    if (!listBuildables && !renderCmd) {
        return;
    }
    yMake.Conf.Cmsg() << "** Build summary **\n";
    TVector<std::pair<size_t, TNodeId>> NodesByDepth;
    for (auto&& i : printer.Nodes) {
        if (!i.second.HasBuildFrom || !i.second.IsFile) {
            continue;
        }
        NodesByDepth.emplace_back(i.second.Depth, i.first);
    }
    std::sort(NodesByDepth.begin(), NodesByDepth.end());
    TMakeModuleSequentialStates modulesStatesCache{yMake.Conf, yMake.Graph, yMake.Modules};
    for (auto&& i : NodesByDepth) {
        if (renderCmd) {
            TSubst2Shell cmdImage;
            TMakeCommand mkCmd(modulesStatesCache, yMake);
            mkCmd.CmdInfo.MkCmdAcceptor = &cmdImage;
            try {
                mkCmd.GetFromGraph(i.second, printer.Node2Module[i.second]);
                cmdImage.PrintAsLine(yMake.Conf.Cmsg()) << Endl;
            } catch (yexception& e) {
                yMake.Conf.Cmsg() << "#Error: " << yMake.Graph.ToString(yMake.Graph.Get(i.second)) << ": " << e.what() << "\n";
            }
        }
        if (listBuildables) {
            yMake.Conf.Cmsg() << yMake.Graph.ToString(yMake.Graph.Get(i.second)) << " depth = " << i.first << "\n";
        }
    }

    if (yMake.Conf.DumpPretty)
        yMake.Conf.Cmsg() << Endl;
}

void TYMake::DumpGraph() {
    if (this->Conf.DumpAsJson) {
        DumpGraphInternal<TJsonFormat>(*this);
    } else if (this->Conf.DumpAsDot) {
        Conf.Cmsg() << "digraph d {" << Endl;
        DumpGraphInternal<TDotFormat<false>>(*this);
        DumpGraphInternal<TDotFormat<true>>(*this);
        Conf.Cmsg() << "}" << Endl;
    } else if (this->Conf.DumpGraphFlatJson || this->Conf.DumpGraphFlatJsonWithCmds) {
        DumpGraphInternal<TFlatJsonFormat>(*this);
    } else {
        DumpGraphInternal<THumanReadableFormat>(*this);
    }
}

class TRecurseFilter: public TNoReentryStatsConstVisitor<> {
private:
    using TBase = TNoReentryStatsConstVisitor<>;
    TRestoreContext RestoreContext;
    TDependencyFilter DepFilter;

public:
    TRecurseFilter(TRestoreContext restoreContext, int skipFlags)
        : RestoreContext{restoreContext}
        , DepFilter(skipFlags) {
    }
    bool AcceptDep(TState& state) {
        if (!TBase::AcceptDep(state)) {
            return false;
        }
        const auto dep = state.NextDep();
        if (*dep == EDT_Property || !IsReachableManagedDependency(RestoreContext, dep)) {
            return false;
        }
        return DepFilter(dep, !state.HasIncomingDep());
    }
};

struct TRelationPrinterNodeData : public TEntryStatsData {
    bool IsInRelation;
    explicit TRelationPrinterNodeData(bool isInRelation = false)
        : IsInRelation(isInRelation)
    {
    }
};

struct TDestinationData {
    const THashSet<TNodeId>& DestNodes;
    const TVector<TString>& DestList;

    TDestinationData(const THashSet<TNodeId>& destNodes, const TVector<TString>& destList)
        : DestNodes(destNodes)
        , DestList(destList)
    {
    }

    TDestinationData(const TDestinationData&) = default;
};

struct TPrinterOutputData {
    THolder<NFlatJsonGraph::TWriter> JsonWriter;
    IOutputStream& Cmsg;
};

using TRelationPrinterState = TVisitorStateItem<TRelationPrinterNodeData>;

class TRelationPrinter: public TNoReentryVisitorBase<TRelationPrinterState, TGraphConstIteratorStateItemBase> {
protected:
    using TBase = TNoReentryVisitorBase<TRelationPrinterState, TGraphConstIteratorStateItemBase>;

    TRestoreContext RestoreContext;
    TDependencyFilter DepFilter;
    TDestinationData DestData;
    TPrinterOutputData& OutputData;
    const THashSet<TTarget>& ModuleStartTargets;
    bool MatchByPrefix;
    bool ShowDepsBetweenTargets;

    THashSet<ui32> StartDirsElemIds;
    THashSet<ui32> AllPrintedNodes;

    /// Template inheritance in C++ adds some ugliness: dependent names are not attributed to ancestor
    using TBase::Nodes;
    using TBase::CurEnt;

public:
    TRelationPrinter(TRestoreContext restoreContext,
                     TPrinterOutputData& outputData,
                     const TDebugOptions& cf,
                     TDestinationData destData,
                     const THashSet<TTarget>& moduleStartTargets)
        : RestoreContext{restoreContext}
        , DepFilter(PrepareSkipFlags(cf))
        , DestData(destData)
        , OutputData(outputData)
        , ModuleStartTargets(moduleStartTargets)
        , MatchByPrefix(cf.DumpRelationsByPrefix)
        , ShowDepsBetweenTargets(cf.DumpDepsBetweenTargets)
    {}

    bool AcceptDep(TState& state) {
        bool result = TBase::AcceptDep(state);
        auto dep = state.NextDep();
        bool isStartModuleDep = !state.HasIncomingDep() && IsDirToModuleDep(dep);
        if (isStartModuleDep && !ModuleStartTargets.contains(dep.To().Id())) {
            return false;
        }
        return result && DepFilter(state.NextDep(), !state.HasIncomingDep()) &&
               IsReachableManagedDependency(RestoreContext, state.NextDep());
    }

    bool Enter(TState& state) {
        if (!TBase::Enter(state)) {
            return false;
        }
        if (MatchByPrefix) {
            if (!IsModuleType(state.TopNode()->NodeType)) {
                return true;
            }
            TStringBuf targetStr = NPath::CutAllTypes(state.Top().GetFileName().GetTargetStr());
            for (const auto& prefix : DestData.DestList) {
                if (targetStr.StartsWith(prefix)) {
                    EmitModule(state.Top(), true);
                    return ShowDepsBetweenTargets;
                }
            }
        } else if (DestData.DestNodes.contains(state.TopNode().Id())) {
            EmitModule(state.Top(), true);
            return ShowDepsBetweenTargets;
        }
        return true;
    }

    void Leave(TState& state) {
        TBase::Leave(state);
        if (CurEnt->IsInRelation) {
            if (!state.HasIncomingDep()) {
                StartDirsElemIds.insert(state.TopNode()->ElemId);
                return;
            }
            EmitModule(*state.Parent());
            EmitLink(state.IncomingDep());
        }
    }

    const THashSet<ui32>& GetStartDirs() const {
        return StartDirsElemIds;
    }

    const THashSet<ui32>& GetPrintedNodes() const {
        return AllPrintedNodes;
    }

private:
    static constexpr size_t DOT_STRING_SIZE_LIMIT = 1000;

    void EmitModule(TStateItem& stateItem, bool isTarget = false) {
        auto entry = VisitorEntry(stateItem);
        if (entry->IsInRelation) {
            return;
        }
        entry->IsInRelation = true;

        auto node = stateItem.Node();
        TStringBuf name = stateItem.GetFileName().GetTargetStr();
        if (name.size() > DOT_STRING_SIZE_LIMIT) {
            name = name.SubStr(0, DOT_STRING_SIZE_LIMIT);
        }

        AllPrintedNodes.insert(node->ElemId);

        if (OutputData.JsonWriter) {
                OutputData.JsonWriter->AddNode(node);
        } else {
            OutputData.Cmsg << "\"" << node->ElemId <<"\" [label=\"" << EscapeC(name) <<
                "\\n" << node->ElemId << " " << node->NodeType << "\"" <<
                (isTarget ? ", style=\"bold, rounded\"" : "") << "];" << Endl;
        }
    }

    void EmitLink(TConstDepRef dep) {
        if (OutputData.JsonWriter) {
            OutputData.JsonWriter->AddLink(dep);
        } else {
            OutputData.Cmsg << "\"" << dep.From()->ElemId << "\" -> \""
                << dep.To()->ElemId << "\" [label=\"" << dep.Value() << "\"];" << Endl;
        }
    }
};

class TRecurseRelationsPrinter : public TNoReentryStatsConstVisitor<TRelationPrinterState, TGraphIteratorStateItemBase<true>> {
private:
    static constexpr size_t DOT_STRING_SIZE_LIMIT = 1000;

    TPrinterOutputData& OutputData;
    const THashSet<ui32>& StartDirsIds;
    const THashSet<ui32>& AlreadyPrintedNodes;

public:
    using TBase = TNoReentryStatsConstVisitor<TRelationPrinterState, TGraphIteratorStateItemBase<true>>;
    using TState = typename TBase::TState;

    TRecurseRelationsPrinter(TPrinterOutputData& outputData, const THashSet<ui32>& startDirs, const THashSet<ui32>& printedNodes)
    : OutputData(outputData)
    , StartDirsIds(startDirs)
    , AlreadyPrintedNodes(printedNodes)
    {}

    void Leave(TState& state) {
        if (StartDirsIds.contains(state.TopNode()->ElemId)) {
            CurEnt->IsInRelation = true;
        } else if (CurEnt->IsInRelation && !AlreadyPrintedNodes.contains(state.TopNode()->ElemId)) {
            auto node = state.TopNode();

            TStringBuf name = TDepGraph::Graph(node).ToTargetStringBuf(node);
            if (name.size() > DOT_STRING_SIZE_LIMIT) {
                name = name.SubStr(0, DOT_STRING_SIZE_LIMIT);
            }

            if (OutputData.JsonWriter) {
                OutputData.JsonWriter->AddNode(node);
            } else {
                OutputData.Cmsg << "\"" << node->ElemId <<"\" [label=\"" << EscapeC(name) <<
                    "\\n" << node->ElemId << " " << node->NodeType << "\"];" << Endl;
            }
        }
        TBase::Leave(state);
    }

    void Left(TState& state) {
        auto prev = CurEnt;
        TBase::Left(state);
        if (!prev->IsInRelation) {
            return;
        }

        CurEnt->IsInRelation = true;
        const auto dep = state.Top().CurDep();

        if (OutputData.JsonWriter) {
            OutputData.JsonWriter->AddLink(dep);
        } else {
            OutputData.Cmsg << "\"" << dep.From()->ElemId << "\" -> \""
            << dep.To()->ElemId << "\" [label=\"" << dep.Value() << "\"];" << Endl;
        }
    }
};


static void PrintTargetSuggest(const TDepGraph& graph,
                               TStringBuf target,
                               IOutputStream& out,
                               size_t numSuggestions = 5) {
    out << "Target '" << target << "' seems invalid. " << Endl
        <<  "To list all dependencies run: " << Endl
        << "  $ ya dump modules" << Endl
        << "Searching for similar targets..." << Endl;
    using TSuggestion = std::pair<size_t, TNodeId>;
    TTopKeeper<TSuggestion, TLess<TSuggestion>> candidates(numSuggestions);
    for (auto node : graph.ConstNodes()) {
        if (UseFileId(node->NodeType)) {
            TStringBuf name = graph.GetFileName(node->NodeType, node->ElemId).GetTargetStr();
            size_t dist = NLevenshtein::Distance(target, name);
            candidates.Emplace(dist, node.Id());
        }
    }
    if (!candidates.IsEmpty()) {
        out << "Maybe you meant" <<
            (candidates.GetSize() > 1 ? " one of" : "") << ":" << Endl;
        for (auto cand : candidates.Extract()) {
            out << "  " << graph.GetFileName(graph.Get(cand.second)) << Endl;
        }
    }
}

bool TYMake::ResolveRelationTargets(const TVector<TString>& targets, THashSet<TNodeId>& result) {
    TResolveContext ctx(Conf, Graph, TOwnEntries(), IncParserManager.Cache);
    TPathResolver resolver(ctx, Conf.ShouldForceListDirInResolving());
    auto& fileConf = Graph.Names().FileConf;
    TFileView srcDir = fileConf.SrcDir();
    TVector<TFileView> searchDirs(Reserve(Conf.StartDirs.size()*2));
    for (const auto& dir : Conf.StartDirs) {
        searchDirs.emplace_back(fileConf.GetStoredName(ArcPath(dir.GetPath())));
        searchDirs.emplace_back(fileConf.GetStoredName(BuildPath(dir.GetPath())));
    }
    auto plan = MakeResolvePlan(srcDir, fileConf.BldDir(), MakeIterPair(searchDirs));

    bool success = true;
    for (TStringBuf target : targets) {
        TNodeId node = GetUserTarget(target);
        if (node != TNodeId::Invalid) {
            result.insert(node);
            continue;
        }

        TStringBuf query = NPath::IsTypedPath(target) ? NPath::CutType(target) : target;
        auto status = resolver.ResolveName(query, srcDir, plan);
        if (status != RESOLVE_SUCCESS) {
            Conf.Cmsg() << "Target '" << target << "' seems invalid." << Endl;
            PrintTargetSuggest(Graph, target, Conf.Cmsg());
            success = false;
            continue;
        }
        TResolveFile resolved = resolver.Result();
        TStringBuf resolvedTargetStr = resolver.GetTargetBuf(resolved);
        if (NPath::CutType(resolvedTargetStr) != target) {
            Conf.Cmsg() << "Target '" << target << "' is resolved to '" << resolvedTargetStr << "'" << Endl;
        }
        TNodeId nodeId = Graph.GetFileNodeById(resolved.GetElemId()).Id();
        if (nodeId != TNodeId::Invalid) {
            result.insert(nodeId);
        } else {
            Conf.Cmsg() << "Target '" << query << "' is not found in build graph." << Endl;
            success = false;
            continue;
        }
    }
    return success;
}

void DumpAllRelations(const TRestoreContext& restoreContext, const TTraverseStartsContext& traverseContext, const TDepGraph& recurseGraph, const TDestinationData& destData, const TVector<TNodeId>& startTargets) {
    TPrinterOutputData outputData = { nullptr, restoreContext.Conf.Cmsg() };
    if (restoreContext.Conf.DumpAsJson) {
        outputData.JsonWriter = MakeHolder<NFlatJsonGraph::TWriter>(outputData.Cmsg);
    } else {
        outputData.Cmsg << "digraph d {" << Endl << "node [shape=rect]" << Endl;
    }

    TRelationPrinter printer(restoreContext, outputData, restoreContext.Conf, destData, traverseContext.ModuleStartTargets);
    IterateAll(restoreContext.Graph, startTargets, printer);

    TRecurseRelationsPrinter recursePrinter(outputData, printer.GetStartDirs(), printer.GetPrintedNodes());
    IterateAll(recurseGraph, traverseContext.RecurseStartTargets, recursePrinter);

    if (!outputData.JsonWriter) {
        outputData.Cmsg << "}" << Endl;
    }
}

namespace {

    struct TRelationItem {
        EMakeNodeType NodeType;
        EDepType DepType;
        ui32 ElemId;
        TString NodeName;
    };

    TVector<TRelationItem> GetPath(const TVector<TNodeId>& startTargets, const TRestoreContext& restoreContext, const TDebugOptions& cf, const TDestinationData& destData) {
        TVector<TRelationItem> path;

        auto IsTargetNode = [&destData, &restoreContext](const auto& node) {
            if (!UseFileId(node->NodeType)) {
                return false;
            }
            if (restoreContext.Conf.DumpRelationsByPrefix) {
                TStringBuf targetStr = NPath::CutAllTypes(restoreContext.Graph.ToTargetStringBuf(node));
                for (const auto& prefix : destData.DestList) {
                    if (targetStr.StartsWith(prefix)) {
                        return true;
                    }
                }
                return false;
            }
            return destData.DestNodes.contains(node.Id());
        };

        for (const auto& start : startTargets) {
            TGraphConstIteratorState state;
            TRecurseFilter filter(restoreContext, PrepareSkipFlags(cf));
            TDepthGraphIterator<TGraphConstIteratorState, TRecurseFilter> it(restoreContext.Graph, state, filter);

            bool isFound = false;
            for (bool res = it.Init(start); res; res = it.Next()) {
                const auto node = (*it).Node();
                if (IsTargetNode(node)) {
                    using TStateItem = decltype(state)::TItem;
                    TMaybe<TStateItem> prev;
                    for (const auto& st : state.Stack()) { // We need raw collection access to print from bottom of stack
                        path.push_back({
                            .NodeType=st.Node()->NodeType,
                            .DepType=(!prev ? EDepType::EDT_Include : *prev->CurDep()),
                            .ElemId=st.Node()->ElemId,
                            .NodeName=st.Print()
                        });
                        prev = st;
                    }
                    isFound = true;
                    break;
                }
            }
            if (isFound) {
                break;
            }
        }
        return path;
    }

    TVector<TRelationItem> GetRecursePath(const TVector<TTarget>& startTargets, const TDepGraph& graph, ui32 startElemId) {
        TVector<TRelationItem> path;
        using TRelationVisitor = TNoReentryStatsConstVisitor<TRelationPrinterState, TGraphIteratorStateItemBase<true>>;
        for (const auto& start : startTargets) {
            TGraphConstIteratorState state;
            TRelationVisitor visitor;
            TDepthGraphIterator<TGraphConstIteratorState, TRelationVisitor> it(graph, state, visitor);
            for (bool res = it.Init(start.Id); res; res = it.Next()) {
                const auto node = (*it).Node();
                bool isFound = false;
                if (node->ElemId == startElemId) {
                    using TStateItem = decltype(state)::TItem;
                    TMaybe<TStateItem> prev = *state.Stack().begin();
                    for (const auto& st : state.Stack()) { // We need raw collection access to print from bottom of stack
                        if (st.Node()->ElemId == node->ElemId) {
                            break;
                        }
                        path.push_back({
                            .NodeType=st.Node()->NodeType,
                            .DepType=(!prev ? EDepType::EDT_Last : *prev->CurDep()),
                            .ElemId=st.Node()->ElemId,
                            .NodeName=st.Print()
                        });
                        prev = st;
                    }
                    isFound = true;
                    break;
                }
                if (isFound) {
                    break;
                }
            }
        }
        return path;
    }
}

void DumpFirstRelation(const TRestoreContext& restoreContext, const TTraverseStartsContext& traverseContext, const TDepGraph& recurseGraph, const TDebugOptions& cf, const TDestinationData& destData, const TVector<TNodeId>& startTargets, TYMake& yMake) {
    TVector<TRelationItem> path = GetPath(startTargets, restoreContext, cf, destData);
    if (path.empty()) {
        cf.Cmsg() << "Sorry, path not found" << Endl;
        return;
    }

    TVector<TRelationItem> recursePath = GetRecursePath(traverseContext.RecurseStartTargets, recurseGraph, path.begin()->ElemId);

    bool isFirst = true;
    for (const auto& p : {recursePath, path}) {
        for (auto it = p.begin(); it != p.end(); it++) {
            auto name = it->NodeName;
            if (it->NodeType == EMNT_BuildCommand || it->NodeType == EMNT_BuildVariable) {
                name = FormatBuildCommandName(
                    yMake.Graph.GetCmdName(it->ElemId),
                    yMake.Commands,
                    yMake.Graph.Names().CommandConf
                );
            }
            if (isFirst) {
                cf.Cmsg() << it->NodeType << " (Start): " << name;
                isFirst = false;
                continue;
            }
            cf.Cmsg() << " ->\n" << it->NodeType << " (" << it->DepType << "): " << name;
        }
    }
    cf.Cmsg() << Endl;
}

void TYMake::FindPathBetween(const TVector<TString>& startList,
                             const TVector<TString>& endList) {
    THashSet<TNodeId> startTargetSet, endTargetSet;
    if (!startList.empty()) {
        if (!ResolveRelationTargets(startList, startTargetSet)) {
            return;
        }
    } else {
        for (const auto& startTarget : StartTargets) {
            if (startTarget.IsModuleTarget) {
                continue;
            }
            if (!startTarget.IsUserTarget && Conf.SkipAllRecurses)
                continue;
            if (startTarget.IsNonDirTarget)
                continue;
            if (startTarget.IsDependsTarget && !startTarget.IsRecurseTarget && Conf.SkipDepends)
                continue;
            startTargetSet.insert(startTarget.Id);
        }
    }
    TVector<TNodeId> startTargets(startTargetSet.begin(), startTargetSet.end());
    SortBy(startTargets, [&](TNodeId id) { return Graph.GetFileName(Graph.Get(id)); });

    YDebug() << "Find path start targets: ";
    for (const auto& start : startTargetSet) {
        YDebug() << start << ' ';
    }
    YDebug() << Endl;
    if (!Conf.DumpRelationsByPrefix && !ResolveRelationTargets(endList, endTargetSet)) {
        return;
    }
    TDestinationData destData = { endTargetSet, endList };
    if (Conf.DumpAsDot || Conf.DumpAsJson) {
        DumpAllRelations(GetRestoreContext(), GetTraverseStartsContext(), RecurseGraph, destData, startTargets);
        return;
    }
    DumpFirstRelation(GetRestoreContext(), GetTraverseStartsContext(), RecurseGraph, Conf, destData, startTargets, *this);
}

class TModDir {
private:
    mutable bool HashIsValid = true;
    mutable size_t HashValue = 0;
    THashSet<TNodeId> ModDirs;
    TNodeId PrimaryModDir = TNodeId::Invalid; // we use only primary path to print error at the case of missing peerdir

public:
    THashSet<TNodeId>::iterator begin() const {
        return ModDirs.cbegin();
    }

    THashSet<TNodeId>::iterator end() const {
        return ModDirs.cend();
    }

    bool has(TNodeId id) const {
        return ModDirs.contains(id);
    }

    void Push(TNodeId id, bool override = false) {
        if (PrimaryModDir == TNodeId::Invalid || override) {
            PrimaryModDir = id;
        }
        if (ModDirs.insert(id).second) {
            HashIsValid = false;
        }
    }

    bool Empty() const {
        return ModDirs.empty();
    }

    size_t Hash() const {
        if (!HashIsValid) {
            HashValue = 0;
            for (auto id : ModDirs) {
                HashValue += IntHash(ToUnderlying(id));
            }
            HashIsValid = true;
        }
        return HashValue;
    }

    bool operator==(const TModDir& other) const {
         return ModDirs == other.ModDirs;
    }

    TNodeId Primary() const {
        return PrimaryModDir;
    }
};

template<>
void Out<std::reference_wrapper<const TModDir>>(IOutputStream& os, TTypeTraits<std::reference_wrapper<const TModDir>>::TFuncParam md) {
    TVector<TNodeId> nodeIds(md.get().begin(), md.get().end());
    Sort(nodeIds);
    os << "[" << JoinSeq(",", nodeIds) << "]";
}

template <>
struct THash<TModDir> {
    size_t operator()(const TModDir& modDir) const {
        return modDir.Hash();
    }
};

struct TDirAsnEntryStats: public TEntryStats {
    bool DetectorVisited = false;
    TModDir ModDir;
    TSimpleSharedPtr<TUniqDeque<TModDir>> DirsInIncls;
    THashMap<TNodeId, TNodeId> Dir2File;
    TSimpleSharedPtr<TUniqVector<TNodeId>> Peerdirs;
    TDirAsnEntryStats(TItemDebug itemDebug = {}, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
    {
    }
};

class TDirAssignProc: public TNoReentryStatsConstVisitor<TDirAsnEntryStats> {
private:
    using TBase = TNoReentryStatsConstVisitor<TDirAsnEntryStats>;
    friend class TAsnReiter;

private:
    const THashSet<TTarget>& ModuleStartTargets;

public:
    THashMap<TNodeId, TNodeId> Dir2MainModule;

    TDirAssignProc(const THashSet<TTarget>& startTargets) : ModuleStartTargets(startTargets) {}

    bool AcceptDep(TState& state) {
        const TBase::TStateItem::TDepRef dep = state.NextDep();

        if (!state.HasIncomingDep() && IsDirToModuleDep(dep)) {
            if (!ModuleStartTargets.contains(dep.To().Id())) {
                return false;
            }
        }

        return TBase::AcceptDep(state) && (!IsModuleType(dep.From()->NodeType) || !IsModuleType(dep.To()->NodeType));
    }

    bool Enter(TState& state) {
        bool ok = TBase::Enter(state);
        const auto& st = state.Top();
        auto nodeType = st.Node()->NodeType;
        TNodeId sinkId = st.Node().Id();

        if (state.HasIncomingDep()) {
            if (nodeType == EMNT_Directory) {
                CurEnt->ModDir.Push(sinkId);
            }
            auto& pst = *state.Parent();
            auto prevEnt = ((TDirAsnEntryStats*)pst.Cookie);
            const auto incDep = state.IncomingDep();
            TNodeId srcId = pst.Node().Id();
            bool atPeerdir = IsPeerdirDep(incDep);
            bool atDir2Mod = IsDirToModuleDep(incDep);
            bool atGlobalGenerated = IsModuleType(pst.Node()->NodeType) && *incDep == EDT_Search2 && nodeType == EMNT_NonParsedFile;
            if (atPeerdir) {
                for (TNodeId id : prevEnt->ModDir) {
                    auto mDir = Nodes.find(id);
                    AddTo(sinkId, mDir->second.Peerdirs);
                }
            }
            if (atDir2Mod) {
                prevEnt->ModDir.Push(srcId);
                Dir2MainModule.try_emplace(srcId, sinkId);
            }
            if ((CurEnt->ModDir.Empty() && (atPeerdir || atDir2Mod)) || IsModuleOwnNodeDep(incDep) || atGlobalGenerated) {
                for (TNodeId id : prevEnt->ModDir) {
                    CurEnt->ModDir.Push(id, atGlobalGenerated);
                }
            }
        }
        return ok;
    }
};

// This function works properly only when TDirAssignProc was used upon entire workdir
template <class TNodes, class TNodesData>
TNodeId GuessModuleDir(TNodes& nodes, const TConstDepNodeRef& node, TNodesData& cur) { // was TFace::ReassignModules
    if (!cur.IsFile || !cur.ModDir.Empty()) {
        return TNodeId::Invalid;
    }
    if (node->NodeType != EMNT_File) {
        return TNodeId::Invalid;
    }
    const auto& graph = TDepGraph::Graph(node);
    TFileView fname = graph.GetFileName(node);
    if (fname.GetType() == NPath::Unset) {
        return TNodeId::Invalid;
    }
    TStringBuf dirname = NPath::Parent(fname.GetTargetStr());
    while (dirname.size()) {
        ui32 dirElemId = graph.Names().FileConf.GetIdNx(dirname);
        TNodeId dirId = dirElemId ? graph.GetNodeById(EMNT_Directory, dirElemId).Id() : TNodeId::Invalid;
        if (const auto nodesIt = nodes.find(dirId); nodesIt != nodes.end() && nodesIt->second.ModDir.has(dirId)) {
            return dirId;
        }
        dirname = NPath::Parent(dirname);
    }
    return TNodeId::Invalid;
}

class TAsnReiter: public TNoReentryStatsConstVisitor<TDirAsnEntryStats> {
private:
    using TBase = TNoReentryStatsConstVisitor<TDirAsnEntryStats>;

protected:
    TRestoreContext RestoreContext;

public:
    TAsnReiter(TRestoreContext restoreContext, TDirAssignProc& firstProc)
        : RestoreContext{restoreContext}
    {
        Nodes.swap(firstProc.Nodes);
    }

    bool Enter(TState& state) {
        auto& st = state.Top();
        TNodeId id = st.Node().Id();

        auto [i, _] = Nodes.try_emplace(id, true, IsFileType(st.Node()->NodeType));
        bool fresh = !i->second.DetectorVisited;
        i->second.DetectorVisited = true;
        st.Cookie = CurEnt = &i->second;
        return fresh;
    }

    bool AcceptDep(TState&) {
        return true;
    }
};


struct TDirAsnEntryPeerdirStats: public TEntryStats {
    bool DetectorVisited = false;
    bool WasFresh = false;
    TModDir Modules;
    TSimpleSharedPtr<TUniqDeque<TModDir>> ModulesInIncls;
    THashMap<TNodeId, TNodeId> Module2File;
    TDirAsnEntryPeerdirStats(TItemDebug itemDebug = {}, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
    {
    }
};

class TDirAssignPeerdirProc: public TNoReentryStatsConstVisitor<TDirAsnEntryPeerdirStats> {
private:
    using TBase = TNoReentryStatsConstVisitor<TDirAsnEntryPeerdirStats>;
    friend class TMissedPeerDetectProc;

private:
    TRestoreContext RestoreContext;

public:
    THashMap<TNodeId, TUniqVector<TNodeId>> DirToModules;
    TDirAssignPeerdirProc(TRestoreContext restoreContext)
    : RestoreContext(restoreContext)
    {}

    bool AcceptDep(TState& state) {
        const TBase::TStateItem::TDepRef dep = state.NextDep();
        return TBase::AcceptDep(state) && (!IsModuleType(dep.From()->NodeType) || !IsModuleType(dep.To()->NodeType));
    }

    bool Enter(TState& state) {
        bool ok = TBase::Enter(state);
        const auto& st = state.Top();
        const auto& graph = TDepGraph::Graph(st.Node());
        auto nodeType = st.Node()->NodeType;

        if (ok && IsModuleType(st.Node()->NodeType)) {
            auto mod = RestoreContext.Modules.Get(st.Node()->ElemId);
            Y_ASSERT(mod);
            TNodeId moduleDirNodeId = graph.GetFileNodeById(mod->GetDirId()).Id();
            DirToModules[moduleDirNodeId].Push(st.Node().Id());
            CurEnt->Modules.Push(st.Node().Id());
        }
        if (state.HasIncomingDep() && UseFileId(nodeType)) {
            auto& pst = *state.Parent();
            auto prevEnt = ((TDirAsnEntryPeerdirStats*)pst.Cookie);
            const auto incDep = state.IncomingDep();
            bool atGlobalGenerated = IsModuleType(pst.Node()->NodeType) && *incDep == EDT_Search2 && nodeType == EMNT_NonParsedFile;
            if (IsModuleOwnNodeDep(incDep) || atGlobalGenerated) {
                for (TNodeId id : prevEnt->Modules) {
                    CurEnt->Modules.Push(id, atGlobalGenerated);
                }
            }
        }
        return ok;
    }
};

class TMissedPeerDetectProc: public TNoReentryStatsConstVisitor<TDirAsnEntryPeerdirStats> {
private:
    using TBase = TNoReentryStatsConstVisitor<TDirAsnEntryPeerdirStats>;
    TRestoreContext RestoreContext;
    bool HasMissingPeerdirs_ = false;
    const THashMap<TNodeId, TUniqVector<TNodeId>>& DirToModules;

public:
    TMissedPeerDetectProc(TRestoreContext restoreContext, TDirAssignPeerdirProc& firstProc)
        : RestoreContext{restoreContext}
        , DirToModules(firstProc.DirToModules)
    {
        Nodes.swap(firstProc.Nodes);
    }

    bool HasMissingPeerdirs() const {
        return HasMissingPeerdirs_;
    }

    bool AcceptDep(TState& state) {
        const auto dep = state.NextDep();
        if (IsTooldirDep(dep) || IsPeerdirDep(dep)) {
            return false;
        }
        return *dep == EDT_Include || *dep == EDT_BuildFrom || IsGlobalSrcDep(dep);
    }

    bool Enter(TState& state) {
        auto& st = state.Top();
        TNodeId id = st.Node().Id();

        auto [i, _] = Nodes.try_emplace(id, true, IsFileType(st.Node()->NodeType));
        bool fresh = !i->second.DetectorVisited;
        i->second.DetectorVisited = true;
        st.Cookie = CurEnt = &i->second;
        if (fresh) {
            CurEnt->WasFresh = true;
        }
        return fresh;
    }

    void Leave(TState& state) {
        TBase::Leave(state);
        auto& st = state.Top();
        const auto& graph = TDepGraph::Graph(st.Node());
        auto nodeType = st.Node()->NodeType;
        TNodeId nodeId = st.Node().Id();
        if (state.HasIncomingDep()) {
            if (!IsModuleType(nodeType) && IsFileType(nodeType)) {
                CheckSourceDependency(*state.Parent(), nodeId);
            }
            // IsIndirectSrcDep(state.IncomingDep()) is not handled for performance reasons for the following reasons:
            //  * We do not support parsing includes from sources mined by _LATE_GLOB.
            //  * This check is quite expensive and visiting extra deps just for proper code is not great here.
        }
        if (CurEnt->WasFresh && IsModuleType(nodeType) && CurEnt->ModulesInIncls) {
            CurEnt->WasFresh = false;
            TFileView modName = graph.GetFileName(st.Node());
            if (RestoreContext.Modules.Get(st.Node()->ElemId)->GetAttrs().DontResolveIncludes) {
                YDIAG(V) << "For now ignore errors about missing PEERDIRs from this kind of module: " << modName << Endl;
                return;
            }

            TScopedContext context(modName); // to use LWARN here
            TModuleRestorer restorer(RestoreContext, st.Node());
            restorer.RestoreModule();
            const auto& peers = restorer.GetPeers();
            for (const auto& modulesGroup : *CurEnt->ModulesInIncls) {
                TString modErrMsg;
                bool resolved = CurEnt->Modules.Empty();
                TVector<TNodeId> fileModules;
                for (TNodeId mod : modulesGroup) {
                    if (CurEnt->Modules.has(mod) || peers.has(mod)) {
                        resolved = true;
                        break;
                    }
                    fileModules.push_back(mod);
                }
                if (resolved) {
                    continue;
                }
                for (TNodeId mod : modulesGroup) {
                    if (CheckByDirectories(CurEnt->Modules, peers, mod)) {
                        resolved = true;
                        break;
                    }
                }
                if (resolved) {
                    continue;
                }

                for (TNodeId mod : fileModules) {
                    if (modErrMsg) {
                        modErrMsg = TString::Join(modErrMsg, ", [[imp]]",  graph.ToTargetStringBuf(mod), "[[rst]]");
                    } else {
                        const auto mod2fileIt = CurEnt->Module2File.find(mod);
                        modErrMsg = TString::Join("used a file [[imp]]",
                                                  mod2fileIt ? graph.GetFileName(graph.Get(mod2fileIt->second)).GetTargetStr() : "",
                                                  "[[rst]] belonging to modules ([[imp]]", graph.ToTargetStringBuf(mod), "[[rst]]");
                    }
                }

                modErrMsg = TString::Join(modErrMsg, ") which are not reachable by [[alt1]]PEERDIR[[rst]]");
                TGraphConstIteratorState state;
                TRecurseFilter filter(RestoreContext, TDependencyFilter::SkipRecurses | TDependencyFilter::SkipModules | TDependencyFilter::SkipTools);
                TDepthGraphIterator<TGraphConstIteratorState, TRecurseFilter> it(graph, state, filter);
                TUniqVector<TNodeId> modDirs;
                TVector<TString> pathErrMsgs;
                bool skipMod = false;
                for (bool res = it.Init(nodeId); res; res = it.Next()) {
                    auto i = Nodes.find(it->Node().Id());
                    if (i == Nodes.end() || i->second.Modules.Primary() != modulesGroup.Primary()) {
                        continue;
                    }
                    for (const auto& st : state.Stack()) { // We need raw collection access to print from bottom of stack
                        TStringBuf name = UseFileId(st.Node()->NodeType) ? st.GetFileName().GetTargetStr() : st.GetCmdName().GetStr();
                        // XXX temp hack to use FindMissingPeerdirs in autocheck
                        if (name.EndsWith("__init__.py")) {
                            skipMod = true;
                            break;
                        }
                        if (!IsFileType(st.Node()->NodeType)) {
                            name = SkipId(name);
                        }
                        pathErrMsgs.emplace_back(name);

                        TString dir = TString{NPath::Parent(name)};
                        if (InBuildDir(dir)) {
                            dir = ArcPath(NPath::CutType(dir));
                        }
                        if (const auto& dirNode = graph.GetFileNode(dir); dirNode.IsValid() && dirNode.Id() != TNodeId::Invalid) {
                            // it can be 0 when there is a source directory but there is no node in it (no ya.make)
                            modDirs.Push(dirNode.Id());
                        }
                    }
                    break;
                }

                if (!skipMod) {
                    HasMissingPeerdirs_ = true;
                    TString inclPath, guess;
                    for (auto i = pathErrMsgs.cbegin(); i != pathErrMsgs.cend(); ++i) {
                        inclPath += TString("\n[[unimp]][  Path][[rst]]: ") + *i + TString((i + 1 == pathErrMsgs.cend()) ? "" : " ->");
                    }

                    for (auto cur = modDirs.begin(), prev = cur++; cur != modDirs.end(); prev = cur++) {
                        auto pDir = DirToModules.find(*prev);
                        if (pDir != DirToModules.end()) {
                            bool inPeerdirs = false;
                            for (const auto& mod : pDir->second) {
                                auto node = Nodes.find(mod);
                                Y_ASSERT(node);
                                TModuleRestorer nodeRestorer(RestoreContext, RestoreContext.Graph.Get(node->first));
                                nodeRestorer.RestoreModule();
                                THashSet<TNodeId> peerDirIds;
                                nodeRestorer.GetPeerDirIds(peerDirIds);
                                if (peerDirIds.contains(*cur)) {
                                    inPeerdirs = true;
                                    break;
                                }
                            }
                            if (!inPeerdirs) {
                                guess += TString("\n[[unimp]][ Guess][[rst]]: [[alt1]]PEERDIR[[rst]] is either missing or forbidden by peerdir policy: ")
                                      + graph.GetFileName(graph.Get(*prev)).GetTargetStr()
                                      + " -> "
                                      + graph.GetFileName(graph.Get(*cur)).GetTargetStr();
                            }
                        }
                    }
                    YConfErr(ChkPeers) << modErrMsg << guess << inclPath << Endl;
                }
            }
        }
    }

private:
    void CheckSourceDependency(TGraphConstIteratorStateItemBase& pst, TNodeId nodeId) {
        TDirAsnEntryPeerdirStats* pEnt = (TDirAsnEntryPeerdirStats*)pst.Cookie;
        size_t prevLen = pEnt->ModulesInIncls ? pEnt->ModulesInIncls->size() : 0;
        if (!CurEnt->Modules.Empty()) {
            AddTo(CurEnt->Modules, pEnt->ModulesInIncls);
        }
        AddTo(CurEnt->ModulesInIncls, pEnt->ModulesInIncls);
        if (pEnt->ModulesInIncls && pEnt->ModulesInIncls->size() > prevLen) {
            if (CurEnt->ModulesInIncls) {
                for (const auto& modulesGroup : *CurEnt->ModulesInIncls) {
                    for (auto mod : modulesGroup) {
                        const auto mod2fileIt = CurEnt->Module2File.find(mod);
                        pEnt->Module2File[mod] = mod2fileIt ? mod2fileIt->second : nodeId;
                    }
                }
            }
            for (TNodeId id : CurEnt->Modules) {
                pEnt->Module2File[id] = nodeId;
            }
        }
    }

    bool CheckByDirectories(const TModDir& modules, const TUniqVector<TNodeId>& peers, TNodeId fileModId) {
        TModuleRestorer fileModRestorer(RestoreContext, RestoreContext.Graph.Get(fileModId));
        auto fileModule = fileModRestorer.RestoreModule();
        ui32 fileDirId = fileModule->GetDirId();
        for (TNodeId mod : modules) {
            TModuleRestorer restorer(RestoreContext, RestoreContext.Graph.Get(mod));
            ui32 dir = restorer.RestoreModule()->GetDirId();
            if (fileDirId == dir) {
                return true;
            }
        }
        for (TNodeId mod : peers) {
            TModuleRestorer restorer(RestoreContext, RestoreContext.Graph.Get(mod));
            ui32 dir = restorer.RestoreModule()->GetDirId();
            if (fileDirId == dir) {
                return true;
            }
        }
        return false;
    }
};

void TYMake::FindMissingPeerdirs() {
    NYMake::TTraceStage stage("Find missing peerdirs");
    TDirAssignPeerdirProc assignProc(GetRestoreContext());
    IterateAll(Graph, StartTargets, assignProc, [](const TTarget& t) -> bool { return t.IsModuleTarget; });

    TMissedPeerDetectProc detectProc(GetRestoreContext(), assignProc);
    IterateAll(Graph, StartTargets, detectProc, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    if (detectProc.HasMissingPeerdirs()) {
        YInfo() << "More information about missing peerdirs can be found on https://wiki.yandex-team.ru/Development/Poisk/arcadia/ymake/manual/missing_peerdirs" << Endl;
    }
}


class TAssignS2MProc: public TAsnReiter {
private:
    using TBase = TAsnReiter;

public:
    THashMap<TNodeId, TVector<TNodeId>> Mod2Srcs;
    THashMap<TNodeId, TNodeId> Dir2MainModule;

    TAssignS2MProc(TRestoreContext restoreContext, TDirAssignProc& firstProc)
        : TAsnReiter(restoreContext, firstProc)
    {
        Dir2MainModule.swap(firstProc.Dir2MainModule);
    }

    bool Enter(TState& state) {
        bool fresh = TBase::Enter(state);

        if (fresh && CurEnt->ModDir.Empty()) {
            TNodeId modDir = GuessModuleDir(Nodes, state.TopNode(), *CurEnt);
            if (auto i = modDir != TNodeId::Invalid ? Dir2MainModule.find(modDir) : Dir2MainModule.end()) {
                Mod2Srcs[i->second].push_back(state.TopNode().Id());
            }
        }
        return fresh;
    }
};

void TYMake::AssignSrcsToModules(THashMap<TNodeId, TVector<TNodeId>>& mod2Srcs) {
    TDirAssignProc assignProc(ModuleStartTargets);
    IterateAll(Graph, StartTargets, assignProc, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });

    TAssignS2MProc assign2Proc(GetRestoreContext(), assignProc);
    IterateAll(Graph, StartTargets, assign2Proc, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    mod2Srcs.swap(assign2Proc.Mod2Srcs);
}

bool TDepDirsProc::Enter(TState& state) {
    bool first = TBase::Enter(state);
    auto nodeType = state.TopNode()->NodeType;
    return first && (IsDirType(nodeType) || IsModuleType(nodeType));
}

bool TDepDirsProc::AcceptDep(TState& state) {
    const auto dep = state.Top().CurDep();
    if (IsTooldirDep(dep) || (IsModuleType(dep.From()->NodeType) && IsModuleType(dep.To()->NodeType))) {
        return false;
    }

    bool ispeer = IsPeerdirDep(dep);
    bool isrecurse = IsPureRecurseDep(dep);
    if (ispeer || isrecurse) {
        bool addDep = ispeer && Mode != DM_RecurseOnly || isrecurse && Mode == DM_RecurseOnly;
        if (addDep) {
            TNodeId id = dep.From().Id();
            DependentDirs[id].push_back(std::make_pair(dep.To().Id(), ispeer ? DDT_Peerdir : DDT_Recurse));
        }
        return addDep;
    }
    return TBase::AcceptDep(state);
}

struct TDepDirsStatsData : public TEntryStatsData {
    bool HasModule = false;
};

using TDepDirsStats = TVisitorStateItem<TDepDirsStatsData>;
class TRecurseDirsProc : public TNoReentryStatsConstVisitor<TDepDirsStats, TGraphIteratorStateItemBase<true>> {
private:
 THashSet<ui32> DirsIds;

public:
    using TBase = TNoReentryStatsConstVisitor<TDepDirsStats, TGraphIteratorStateItemBase<true>>;
    using TState = typename TBase::TState;

    TRecurseDirsProc() = default;

    bool AcceptDep(TState& state) {
        auto dep = state.NextDep();
        if (IsModuleType(dep.To()->NodeType)) {
            CurEnt->HasModule = true;
            return false;
        }
        return TBase::AcceptDep(state);
    }

    void Leave(TState& state) {
        TBase::Leave(state);
        if (CurEnt->HasModule) {
            DirsIds.insert(state.TopNode()->ElemId);
        }
    }

    void Left(TState& state) {
        auto prevEnt = CurEnt;
        TBase::Left(state);
        if (prevEnt->HasModule) {
            CurEnt->HasModule = true;
        }
    }

    const THashSet<ui32>& GetRecurseDirs() {
        return DirsIds;
    }
};

void TYMake::DumpSrcDeps(IOutputStream& cmsg) {
    TFoldersTree fetchedDirs;
    THashSet<ui32> filesToCheckIds;
    TGraphConstIteratorState state;
    TManagedPeerConstVisitor<> visitor{GetRestoreContext(), TDependencyFilter{PrepareSkipFlags(Conf)}};
    for (const auto& target : ModuleStartTargets) {
        TDepthGraphIterator<TGraphConstIteratorState, TManagedPeerConstVisitor<>> it{Graph, state, visitor};
        for (bool res = it.Init(target.Id); res; res = it.Next()) {
            if (!IsModule(*it)) {
                continue;
            }
            const auto* module = Modules.Get(it->Node()->ElemId);
            Y_ASSERT(module);

            for (auto srcDir : module->SrcDirs) {
                fetchedDirs.Add(srcDir);
            }

            for (const auto& [_, incDirsByLang] : module->IncDirs.GetAll()) {
                for (auto incDir : incDirsByLang.Get()) {
                    fetchedDirs.Add(incDir);
                }
            }

            for (auto peer : module->Peers) {
                fetchedDirs.Add(peer);
            }

            if (module->DataPaths) {
                for (auto dataPath : *module->DataPaths) {
                    auto& fileConf = Graph.Names().FileConf;
                    auto filename = dataPath.GetTargetStr();
                    if (fileConf.CheckDirectory(dataPath)) {
                        fetchedDirs.Add(filename);
                    } else {
                        filesToCheckIds.emplace(dataPath.GetElemId());
                    }
                }
            }

            if (Conf.WithYaMake) {
                cmsg << "MakeFile: " << module->GetMakefile().GetTargetStr() << Endl;
            }
        }
    }

    for (const auto& dirPath : fetchedDirs.GetPaths()) {
        cmsg << "Directory: " << dirPath << Endl;
    }

    for (auto filepathId : filesToCheckIds) {
        auto filepath = Graph.Names().FileNameById(filepathId).GetTargetStr();
        if (!fetchedDirs.ExistsParentPathOf(filepath)) {
            cmsg << "File: " << filepath << Endl;
        }
    }

    TNodePrinter<THumanReadableFormat> printer{GetRestoreContext(), Names, ModuleStartTargets, cmsg, Conf, Commands, &fetchedDirs, &filesToCheckIds};
    IterateAll(Graph, ModuleStartTargets, printer);
}

void TYMake::DumpDependentDirs(IOutputStream& cmsg, bool skipDepends) const {
    if (HasNonDirTargets) {
        return;
    }

    TDepDirsProc proc(Conf);
    bool (*filter)(const TTarget&) = nullptr;
    if (skipDepends) {
        filter = [](const TTarget& t) -> bool { return t.IsRecurseTarget && t.IsModuleTarget; };
    } else {
        filter = [](const TTarget& t) -> bool { return t.IsModuleTarget; };
    }
    IterateAll(Graph, StartTargets, proc, filter);

    TUniqVector<TString> depDirs;
    for (const auto& target : StartTargets) {
        if (filter(target)) {
            auto mod = Modules.Get(Graph.Get(target.Id)->ElemId);
            Y_ASSERT(mod);
            depDirs.Push(TString{mod->GetDir().CutType()});
            for (const auto& moduleDepDirs : proc.GetDependentDirs()) {
                for (const auto& depDir : moduleDepDirs.second) {
                    depDirs.Push(TString{Graph.GetFileName(Graph.Get(depDir.first)).CutType()});
                }
            }
        }
    }

    TRecurseDirsProc recurseProc;
    IterateAll(RecurseGraph, RecurseStartTargets, recurseProc);
    for (ui32 recurseId : recurseProc.GetRecurseDirs()) {
        depDirs.Push(TString{Graph.GetFileName(recurseId).CutType()});
    }

    TVector<TString> depDirsSorted = depDirs.Data();
    Sort(depDirsSorted);

    NJsonWriter::TBuf jsonWriter(NJsonWriter::HEM_RELAXED, &cmsg);
    jsonWriter.SetIndentSpaces(2);
    jsonWriter.BeginList();
    for (const auto& dir : depDirsSorted) {
        jsonWriter.WriteString(dir);
    }
    jsonWriter.EndList();
}

bool TBuildTargetDepsPrinter::Enter(TState& state) {
    bool first = TBase::Enter(state);
    CurEnt->WasFresh = first;
    TNodeId nodeId = state.TopNode().Id();
    if (first) {
        if (const auto* loopId = Loops.FindLoopForNode(nodeId)) {
            CurEnt->LoopId = *loopId;
        }
    }
    if (first && IsModule(state.Top())) {
        CurEnt->IsModule = true;
    }
    return first;
}

void TBuildTargetDepsPrinter::Leave(TState& state) {
    const auto node = state.TopNode();
    const auto& graph = TDepGraph::Graph(node);
    if (CurEnt->WasFresh) {
        if (CurEnt->LoopId != TNodeId::Invalid) {
            YDIAG(Dev) << graph.GetFileName(node) << " is in loop " << CurEnt->LoopId << Endl;
            bool sameLoop = state.HasIncomingDep() && ((TBuildTargetDepsEntStats*)(*state.Parent()).Cookie)->LoopId == CurEnt->LoopId;
            if (!sameLoop) {
                TGraphLoop& loop = Loops[CurEnt->LoopId];
                if (!loop.DepsDone) {
                    for (auto l : loop) {
                        if (l != node.Id()) { //not elegantly but working
                            Nodes.at(l).InclFiles.insert(CurEnt->InclFiles.begin(), CurEnt->InclFiles.end());
                            CurEnt->InclFiles.insert(Nodes.at(l).InclFiles.begin(), Nodes.at(l).InclFiles.end());
                            CurEnt->InclFiles.insert(l);
                            Nodes.at(l).InclFiles.insert(node.Id());
                        }
                    }
                    loop.DepsDone = true;
                }
            }
        }
        CurEnt->WasFresh = false;
        if (CurEnt->IsModule) {
            Cmsg << graph.GetFileName(node) << ": module sources: " << Endl;
            for (auto n : CurEnt->BFFiles) {
                Cmsg << "    " << graph.GetFileName(graph.Get(n)) << Endl;
            }
        } else if (CurEnt->HasBuildCmd && CurEnt->IsFile) {
            Cmsg << graph.GetFileName(node) << ": generated file sources: " << Endl;
            for (auto n : CurEnt->BFFiles) {
                Cmsg << "    B " << graph.GetFileName(graph.Get(n)) << Endl;
            }
            for (auto n : CurEnt->InclFiles) {
                Cmsg << "    I " << graph.GetFileName(graph.Get(n)) << Endl;
            }
        }
    }
    TBase::Leave(state);
}

void TBuildTargetDepsPrinter::Left(TState& state) {
    TBuildTargetDepsEntStats* childEnt = CurEnt;
    TBase::Left(state);
    const auto node = state.Top().Node();
    const auto dep = state.Top().CurDep();
    if (CurEnt->WasFresh) {
        auto depType = *dep;
        const auto leftNode = dep.To(); // This is the node we left
        if (IsDirType(node->NodeType) && depType == EDT_Include && childEnt->IsModule) {
            CurEnt->InclFiles.insert(leftNode.Id());
        } else if (CurEnt->IsFile) {
            if (CurEnt->IsModule) {
                //for module buildfrom are peerdirs and objs
                if ((depType == EDT_BuildFrom || depType == EDT_Include) && IsDirType(leftNode->NodeType)) { //is peerdir
                    CurEnt->BFFiles.insert(childEnt->InclFiles.begin(), childEnt->InclFiles.end());
                } else if (depType == EDT_BuildFrom && childEnt->IsFile) {
                    CurEnt->BFFiles.insert(leftNode.Id());
                }
            } else if (childEnt->IsFile && depType == EDT_BuildFrom || depType == EDT_Include) {
                if (depType == EDT_Include) {
                    CurEnt->InclFiles.insert(leftNode.Id());
                } else {
                    CurEnt->BFFiles.insert(leftNode.Id());
                }
                CurEnt->InclFiles.insert(childEnt->InclFiles.begin(), childEnt->InclFiles.end());
            }

            if (IsIndirectSrcDep(dep)) {
                IterateIndirectSrcs(dep, [&](const auto& srcNode) {
                    CurEnt->BFFiles.insert(srcNode.Id());
                });
            }
        }
    }
}

bool TBuildTargetDepsPrinter::AcceptDep(TState& state) {
    const auto dep = state.NextDep();
    if (*dep == EDT_Search || *dep == EDT_Search2 || *dep == EDT_Property || *dep == EDT_OutTogetherBack) {
        return false;
    }

    if (IsRecurseDep(dep)) {
        return false;
    }

    return TBase::AcceptDep(state);
}

void TYMake::PrintTargetDeps(IOutputStream& cmsg) const {
    if (HasNonDirTargets) {
        return;
    }
    auto loops = TGraphLoops::Find(Graph, StartTargets, true);
    TBuildTargetDepsPrinter proc(loops, cmsg);
    IterateAll(Graph, StartTargets, proc, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
}

void TYMake::DumpBuildTargets(IOutputStream& cmsg) const {
    for (const auto& target : StartTargets) {
        if (!target.IsModuleTarget) {
            continue;
        }
        TVector<TNodeId> moduleIds, glSrcIds;
        ListTargetResults(target, moduleIds, glSrcIds);
        if (moduleIds.empty()) {
            YInfo() << "Nothing to build for " << Graph.GetFileName(Graph.Get(target)) << Endl;
        }

        for (const auto& mod : moduleIds) {
            cmsg << Conf.RealPath(Graph.GetFileName(Graph.Get(mod))) << Endl;
        }

        cmsg << "Will also build:" << Endl;
        for (const auto& obj : glSrcIds) {
            cmsg << Conf.RealPath(Graph.GetFileName(Graph.Get(obj))) << Endl;
        }
    }
}

TString TDumpDartProc::SubstModuleVars(const TStringBuf& data, const TModule& module) const {
    TString dartData;
    TStringBuf name, value, finish;
    constexpr auto beginDelim = ": ";
    constexpr auto endDelim = '\n';
    if (Option != EOption::SubstVarsGlobaly) {
        data.Split(beginDelim, name, value);
        value.Split(endDelim, value, finish);

        dartData = Option == EOption::Encoded ? Base64Decode(value) : TString(value);
    } else {
        dartData = TString{data};
    }

    auto moduleDirsVars = module.ModuleDirsToVars();
    TCommandInfo cmdInfo(RestoreContext.Conf, &RestoreContext.Graph, nullptr);

    NJson::TJsonValue jsonData;
    if (NJson::ReadJsonTree(dartData, &jsonData)) {
        PatchStrings([&](TString s) {
            s = cmdInfo.SubstMacroDeeply(nullptr, s, moduleDirsVars, false, ECF_Unset);
            s = cmdInfo.SubstMacroDeeply(nullptr, s, module.Vars, false, ECF_Unset);
            return s;
        }, jsonData);
        dartData = NJson::WriteJson(jsonData, false);
    } else {
        dartData = cmdInfo.SubstMacroDeeply(nullptr, dartData, moduleDirsVars);
        dartData = cmdInfo.SubstMacroDeeply(nullptr, dartData, module.Vars);
    }

    if (Option == EOption::Encoded) {
        dartData = Base64Encode(dartData);
    }

    return Option != EOption::SubstVarsGlobaly ? TString::Join(name, beginDelim, dartData, endDelim, finish) : dartData;
}

bool TDumpDartProc::AcceptDep(TState& state) {
    const auto dep = state.NextDep();
    if (IsDirToModuleDep(dep) && state.Size() > 1) {
        return false;
    }

    return TBase::AcceptDep(state);
}

bool TDumpDartProc::Enter(TState& state) {
    bool first = TBase::Enter(state);
    if (!first) {
        return false;
    }
    const auto node = state.TopNode();
    if (node->NodeType == EMNT_Property) {
        THashMap<TString, TString> globalResources;
        TStringBuf propVal = TDepGraph::GetCmdName(state.TopNode()).GetStr();
        if (GetPropertyName(propVal) == DartPropertyName) {
            TVars vars;
            const auto moduleIt = FindModule(state);
            AssertEx(moduleIt != state.end(), "Can't find module");
            TModuleRestorer restorer(RestoreContext, moduleIt->Node());
            const auto module = restorer.RestoreModule();
            TScopedContext context(module->GetMakefile());
            restorer.UpdateLocalVarsFromModule(vars, RestoreContext.Conf, false);
            restorer.UpdateGlobalVarsFromModule(vars);
            for (const auto& gvar: vars) {
                if (NYMake::IsGlobalResource(gvar.first)) {
                    auto [it, added] = globalResources.try_emplace(gvar.first);
                    if (added) {
                        it->second = JoinSeq(" ", EvalAll(gvar.second, vars, Commands, RestoreContext.Graph.Names().CommandConf, RestoreContext.Conf));
                    }
                }
            }
            const auto propertyValue = GetPropertyValue(propVal);
            auto dartData = SubstModuleVars(propertyValue, *module);
            for (const auto& igvar: globalResources) {
                dartData = TString::Join(igvar.first, ": ", igvar.second, "\n", dartData);
            }
            Out << dartData;
        }
    }
    return first;
}

bool TDumpMakeFileDartVisitor::AcceptDep(TState& state) {
    const auto dep = state.NextDep();
    if (IsRecurseDep(dep) || IsSearchDirDep(dep)) {
        return false;
    }

    if (!state.HasIncomingDep() && IsDirToModuleDep(dep)) {
        if (!ModuleStartTargets.contains(dep.To().Id())) {
            return false;
        }
    }

    return TBase::AcceptDep(state);
}

bool TDumpMakeFileDartVisitor::Enter(TState& state) {
    const bool first = TBase::Enter(state);
    if (!state.HasIncomingDep() && !RecurseDirs.contains(state.TopNode()->ElemId)) {
        return false;
    }
    auto& st = state.Top();
    const auto node = st.Node();
    st.Fresh = first;
    if (node->NodeType == EMNT_Property && state.HasIncomingDep()) {
        const TStringBuf propVal = st.GetCmdName().GetStr();
        if (GetPropertyName(propVal) == TStringBuf("OWNER")) {
            state.Parent()->OwnersNode = *node;
        }
    }
    return first;
}

void TDumpMakeFileDartVisitor::Leave(TState& state) {
    TBase::Leave(state);
    const auto& st = state.Top();
    const auto node = st.Node();
    if (st.Fresh && node->NodeType == EMNT_MakeFile) {
        const auto& graph = TDepGraph::Graph(node);
        const TStringBuf name = graph.GetFileName(node).GetTargetStr();
        Out << DELIMITER << "\n";
        Out << "PATH: " << NPath::Parent(name) << "\n";
        if (st.OwnersNode.ElemId) {
            Out << "OWNERS: " << GetPropertyValue(graph.GetCmdName(st.OwnersNode).GetStr()) << "\n";
        }
    }
}

bool TDumpDartProcStartTargets::AcceptDep(TState& state) {
    const auto dep = state.NextDep();
    const bool ac = IsDirToModuleDep(dep) || IsModule(state.Top()) && *dep == EDT_Property
        || (RestoreContext.Conf.ShouldAddPeerdirsGenTests() && IsDirectPeerdirDep(dep));
    return ac && TBase::AcceptDep(state);
}

class TFilteredOutputStream: public IOutputStream {
    static constexpr TStringBuf Marker = "CUSTOM-DEPENDENCIES:";
public:
    TFilteredOutputStream(IOutputStream& stream, const TDepGraph& graph, const TDependsToModulesClosure& closure, bool validateDepends)
        : OutputStream(stream)
        , Graph(graph)
        , Closure(closure)
        , ValidateDepends(validateDepends)
    {
    }
protected:
    void DoWrite(const void* buffer, size_t length) override {
        // Fix up 'CUSTOM-DEPENDECIES:' property according to DEPENDS to modules
        // closures collected. If a directory is in the Closure map than add
        // the whole closure to the property
        TStringBuf str(static_cast<const char*>(buffer), length);
        TStringBuf line;
        while (str.ReadLine(line)) {
            if (!line.StartsWith(Marker)) {
                OutputStream << line << '\n';
                continue;
            }

            TVector<TStringBuf> dirs;
            Split(line.SubStr(Marker.length()), " ", dirs);

            TVector<TStringBuf> deps;
            for (const auto dir : dirs) {
                auto iter = Closure.find(dir);
                if (iter != Closure.end()) {
                    for (const auto nodeId : iter->second) {
                        deps.push_back(Graph.GetFileName(Graph.Get(nodeId)).CutType());
                    }
                } else if (ValidateDepends) {
                    YConfErr(Misconfiguration) << "Test dependency to [[imp]]" << dir << "[[rst]] is not listed as [[alt1]]DEPENDS[[rst]]" << Endl;
                }
            }

            if (deps.empty()) {
                continue;
            }

            SortUnique(deps);

            OutputStream << Marker;
            for (const auto& d : deps) {
                OutputStream << ' ' << d;
            }
            OutputStream << '\n';
        }
    }
private:
    IOutputStream& OutputStream;
    const TDepGraph& Graph;
    const TDependsToModulesClosure& Closure;
    bool ValidateDepends;
};

void TYMake::DumpTestDart(IOutputStream& cmsg) {
    if (HasNonDirTargets) {
        return;
    }
    // Wrap up the output stream cmsg to a special adaptor which will
    // update CUSTOM-DEPENDECIES property (apply DEPENDS to modules closure
    // for each directory from the list of CUSTOM-DEPENDECIES property
    // if there is one in the DEPENDS to modules closure map)
    TFilteredOutputStream out(cmsg, Graph, DependsToModulesClosure, Conf.ShouldCheckDependsInDart());
    TDumpDartProcStartTargets proc(GetRestoreContext(), Commands, out, "DART_DATA", TDumpDartProc::SubstVarsGlobaly);
    IterateAll(Graph, StartTargets, proc, [](const TTarget& t) -> bool { return (!t.IsDependsTarget || t.IsRecurseTarget) && t.IsModuleTarget; });
}

void TYMake::DumpJavaDart(IOutputStream& cmsg) {
    TDumpDartProc proc(GetRestoreContext(), Commands, cmsg, "JAVA_DART_DATA", TDumpDartProc::Encoded);
    IterateAll(Graph, StartTargets, proc, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
}

void TYMake::DumpMakeFilesDart(IOutputStream& cmsg) {
    TRecurseDirsProc recurseDirsProc;
    IterateAll(RecurseGraph, RecurseStartTargets, recurseDirsProc);
    TDumpMakeFileDartVisitor proc(cmsg, recurseDirsProc.GetRecurseDirs(), ModuleStartTargets);
    IterateAll(Graph, StartTargets, proc, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });
}

void DumpModulesInfo(IOutputStream& out, const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TString& filter) {
    TGraphConstIteratorState state;
    TManagedPeerConstVisitor<> visitor{restoreContext, TDependencyFilter{PrepareSkipFlags(restoreContext.Conf)}};
    const auto filterRe = filter.empty() ? TMaybe<TRegExMatch>{} : TMaybe<TRegExMatch>{NMaybe::TInPlace{}, filter};
    NJsonWriter::TBuf json(NJsonWriter::HEM_DONT_ESCAPE_HTML, &out);
    if (restoreContext.Conf.DumpAsJson) {
        json.SetIndentSpaces(2);
        json.BeginList();
    }
    for (const auto& target: startTargets) {
        if (!target.IsModuleTarget) {
            continue;
        }
        TDepthGraphIterator<TGraphConstIteratorState, TManagedPeerConstVisitor<>> it(restoreContext.Graph, state, visitor);
        for (bool res = it.Init(target.Id); res; res = it.Next()) {
            if (!IsModule(*it)) {
                continue;
            }
            const auto* module = restoreContext.Modules.Get(it->Node()->ElemId);
            Y_ASSERT(module);

            TString moduleName = TString{module->GetName().GetTargetStr()};
            if (filterRe && !filterRe->Match(moduleName.c_str())) {
                continue;
            }
            if (restoreContext.Conf.DumpAsJson) {
                DumpModuleInfoJson(json, *module);
            } else {
                DumpModuleInfo(out, *module);
            }
        }
    }
    if (restoreContext.Conf.DumpAsJson) {
        json.EndList();
        out << Endl;
    }
}

TString DumpNodeFlags(ui32 elemId, EMakeNodeType nodeType, const TSymbols& names) {
    TString result;
    if (!UseFileId(nodeType)) {
        auto& data = names.CommandConf.GetById(TVersionedCmdId(elemId).CmdId());
        if (data.KeepTargetPlatform)
            result = "KTP";
    }
    return result;
}

template <>
void Out<TDartManager::EDartType>(IOutputStream& out, TDartManager::EDartType dartType) {
    switch (dartType) {
    case TDartManager::EDartType::None:
        out << "NoneDart";
        break;
    case TDartManager::EDartType::Test:
        out << "TestDart";
        break;
    case TDartManager::EDartType::Java:
        out << "JavaDart";
        break;
    case TDartManager::EDartType::Makefiles:
        out << "MakefilesDart";
        break;
    default:
        out << "UnknownDart";
        break;
    }
}

void TDartManager::Dump(EDartType dartType, const TString& dartName) {
    TFsPath dartPath{dartName};
    TFsPath cachePath = YMake_.Conf.YmakeDartsCacheDir / (dartPath.Basename() + ".cache");
    TString dartTypeStr = ToString(dartType);
    Y_ASSERT(cachePath != dartPath);
    if (YMake_.Conf.ShouldLoadDartCaches() && YMake_.CanBypassConfigure() && cachePath.Exists()) {
        NYMake::TTraceStage trace{fmt::format("Load {} from cache", dartTypeStr)};
        cachePath.CopyTo(dartName, true);
        return;
    } else {
        NYMake::TTraceStage trace{fmt::format("Render {}", dartTypeStr)};
        THolder<IOutputStream> dartOut;
        dartOut.Reset(new TFileOutput(dartName));
        switch (dartType) {
        case EDartType::Test:
            YMake_.DumpTestDart(*dartOut);
            break;
        case EDartType::Java:
            YMake_.DumpJavaDart(*dartOut);
            break;
        case EDartType::Makefiles:
            YMake_.DumpMakeFilesDart(*dartOut);
            break;
        default:
            // Unreachable code
            Y_ASSERT(0);
        }
        dartOut->Finish();
    }
    if (YMake_.Conf.ShouldSaveDartCaches()) {
        NYMake::TTraceStage trace{fmt::format("Save {} to cache", dartTypeStr)};
        TFsPath{dartName}.CopyTo(cachePath, true);
    }
}
