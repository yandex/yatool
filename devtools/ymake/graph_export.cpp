#include "graph_export.h"
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/macro_string.h>
#include <devtools/ymake/module_restorer.h>
#include <devtools/ymake/module_store.h>
#include "json_subst.h"
#include "mkcmd.h"

#include <fmt/format.h>
#include <util/generic/scope.h>

namespace {

bool IsBuilderNode(TConstDepNodeRef node) {
    if (!IsOutputType(node->NodeType))
        return false;
    if (!node.Edges().IsEmpty() && **node.Edges().begin() == EDT_OutTogether)
        return false;
    for (const auto& e : node.Edges())
        if (IsBuildCommandDep(e))
            return true;
    // we get here for, e.g., NonParsedFile $L/TEXT/$B/....py.yapyc3
    return false;
}

// A "$L/ACTION/" node is the OutTogether grouping anchor of a multi-output command:
// it carries the command's shared properties but is not itself an artifact. Its real
// outputs hang off it via EDT_OutTogetherBack, so the marker must not be exported as
// an output (doing so leaks "$L/ACTION/" and duplicates the main output in the xid).
bool IsActionMarker(TConstDepNodeRef node) {
    return TDepGraph::GetFileName(node).GetContextType() == ELT_Action;
}

TStringBuf ToString(NPath::ERoot root) {
    switch (root) {
        case NPath::Source: return "source";
        case NPath::Build: return "build";
        case NPath::Unset: return "unset";
        case NPath::Link: Y_ASSERT(root != NPath::Link); return "link";
    }
}

void EmitPathKV(NJsonWriter::TBuf& jsonWriter, TFileView path) {
    TStringBuf pathStr = path.GetTargetStr();
    if (!NPath::IsTypedPath(pathStr)) {
        jsonWriter.WriteKey("path");
        jsonWriter.WriteString(pathStr);
    } else if (path.IsLink()) {
        jsonWriter.WriteKey("view");
        jsonWriter.WriteString(path.GetContextFromLink());
        jsonWriter.WriteKey("root");
        jsonWriter.WriteString(ToString(NPath::GetType(pathStr)));
        jsonWriter.WriteKey("path");
        jsonWriter.WriteString(NPath::CutType(pathStr));
    } else {
        jsonWriter.WriteKey("root");
        jsonWriter.WriteString(ToString(path.GetType()));
        jsonWriter.WriteKey("path");
        jsonWriter.WriteString(path.CutType());
    }
}

void EmitPath(NJsonWriter::TBuf& jsonWriter, TFileView path) {
    jsonWriter.BeginObject();
    EmitPathKV(jsonWriter, path);
    jsonWriter.EndObject();
}

auto ForMkfInDir(TConstDepNodeRef directory, auto fn) {
    for (const auto& edge : directory.Edges()) {
        if (*edge == EDT_Include && edge.To()->NodeType == EMNT_MakeFile) {
            fn(edge.To());
            return true;
        }
    }
    return false;
}

TString MkMD5(TStringBuf s) {
    char result[33];
    MD5 md5;
    md5.Update(s);
    md5.End(result);
    return TString(result);
}

auto MkModuleId(TConstDepNodeRef makefile, TStringBuf platform, bool unhash) {
    Y_ASSERT(IsMakeFileType(makefile->NodeType));
    TString path;
    TDepGraph::GetFileName(makefile).GetStr(path);
    auto result = TString::Join("module//", path, "//", platform);
    return unhash ? result : MkMD5(result);
}

auto MkComponentId(TConstDepNodeRef component, TStringBuf platform, bool unhash) {
    Y_ASSERT(IsModuleType(component->NodeType));
    TString path;
    TDepGraph::GetFileName(component).GetStr(path);
    auto result = TString::Join("component//", path, "//", platform);
    return unhash ? result : MkMD5(result);
}

auto MkBuilderId(TConstDepNodeRef builder, TStringBuf platform, bool unhash) {
    Y_ASSERT(IsOutputType(builder->NodeType));
    TString path;
    TDepGraph::GetFileName(builder).GetStr(path);
    auto result = TString::Join(path, "//", platform);
    return unhash ? result : MkMD5(result);
}

//
//
//

class TWriter {
public:
    TWriter(IOutputStream& sink, bool unhash, bool prettify);
    ~TWriter();

public:
    void DumpModule(
        TConstDepNodeRef makefile,
        TConstDepNodeRef directory,
        const THashSet<TFileElemId>* filteredComponents,
        TStringBuf platform
    );
    void DumpComponent(
        TConstDepNodeRef component,
        const TModule& componentStruct,
        const TVector<TConstDepNodeRef>& builders,
        TStringBuf platform,
        TRestoreContext restoreContext
    );
    void DumpBuilder(
        TConstDepNodeRef builder,
        TStringBuf platform,
        auto xidEval
    );
    void DumpFile(
        TConstDepNodeRef file
    );

private:
    static const NJsonWriter::TBufState ZeroState;
    NJsonWriter::TBuf JsonWriter;
    bool Unhash;
};

const NJsonWriter::TBufState TWriter::ZeroState = NJsonWriter::TBuf().State();

TWriter::TWriter(IOutputStream& sink, bool unhash, bool prettify)
    : JsonWriter{NJsonWriter::HEM_RELAXED, &sink}
    , Unhash(unhash)
{
    if (prettify)
        JsonWriter.SetIndentSpaces(2);
}

TWriter::~TWriter() {
}

void TWriter::DumpModule(
    TConstDepNodeRef makefile,
    TConstDepNodeRef directory,
    const THashSet<TFileElemId>* filteredComponents,
    TStringBuf platform
) {
    JsonWriter.BeginObject();
    JsonWriter.WriteKey("id");
    JsonWriter.WriteString(MkModuleId(makefile, platform, Unhash));
    JsonWriter.WriteKey("kind");
    JsonWriter.WriteString("module");
    JsonWriter.WriteKey("directory");
    EmitPath(JsonWriter, TDepGraph::GetFileName(directory));
    JsonWriter.WriteKey("makefile");
    EmitPath(JsonWriter, TDepGraph::GetFileName(makefile));
    TVector<TConstDepNodeRef> recurses, test_recurses;
    for (const auto& mkfEdge : makefile.Edges()) {
        if (IsMakeFilePropertyDep(mkfEdge.From()->NodeType, mkfEdge.Value(), mkfEdge.To()->NodeType)) {
            TConstDepNodeRef prop = mkfEdge.To();
            ui64 propId;
            TStringBuf propType, propValue;
            ParseCommandLikeProperty(TDepGraph::GetCmdName(prop).GetStr(), propId, propType, propValue);
            if (propType == NProps::RECURSES) {
                for (const auto& propEdge : prop.Edges()) {
                    if (IsPropToDirSearchDep(propEdge)) {
                        ForMkfInDir(propEdge.To(), [&](const auto& recMakefile) {
                            recurses.push_back(recMakefile);
                        });
                    }
                }
            } else if (propType == NProps::TEST_RECURSES) {
                for (const auto& propEdge : prop.Edges()) {
                    if (IsPropToDirSearchDep(propEdge)) {
                        ForMkfInDir(propEdge.To(), [&](const auto& recMakefile) {
                            test_recurses.push_back(recMakefile);
                        });
                    }
                }
            }
        }
    }
    if (!recurses.empty()) {
        JsonWriter.WriteKey("recurses");
        JsonWriter.BeginList();
        for (const auto& recurse : recurses)
            JsonWriter.WriteString(MkModuleId(recurse, platform, Unhash));
        JsonWriter.EndList();
    }
    if (!test_recurses.empty()) {
        JsonWriter.WriteKey("test_recurses");
        JsonWriter.BeginList();
        for (const auto& recurse : test_recurses)
            JsonWriter.WriteString(MkModuleId(recurse, platform, Unhash));
        JsonWriter.EndList();
    }
    JsonWriter.WriteKey("platform");
    JsonWriter.WriteString(platform);
    JsonWriter.WriteKey("components");
    JsonWriter.BeginList();
    for (const auto& dirEdge : directory.Edges()) {
        if (filteredComponents && !filteredComponents->contains(AssumeFile(dirEdge.To()->ElemId))) {
            continue;
        }
        if (IsModuleType(dirEdge.To()->NodeType)) {
            JsonWriter.WriteString(MkComponentId(dirEdge.To(), platform, Unhash));
        }
    }
    JsonWriter.EndList();
    JsonWriter.EndObject();
    JsonWriter.Reset(ZeroState);
    JsonWriter.UnsafeWriteRawBytes("\n");
}

void TWriter::DumpComponent(
    TConstDepNodeRef component,
    const TModule& componentStruct,
    const TVector<TConstDepNodeRef>& builders,
    TStringBuf platform,
    TRestoreContext restoreContext
) {
    TVector<TConstDepNodeRef> import;
    TVector<TConstDepNodeRef> reexport;
    TVector<TConstDepNodeRef> auxOutputs;
    TVector<TConstDepNodeRef> depends;
    for (const auto& edge : component.Edges()) {
        if (*edge == EDT_BuildFrom) {
            if (IsModuleType(edge.To()->NodeType)) {
                if (!IsReachableManagedDependency(restoreContext, edge))
                    continue;
                import.push_back(edge.To());
            }
        } else if (*edge == EDT_Include) {
            if (IsModuleType(edge.To()->NodeType)) {
                if (!IsReachableManagedDependency(restoreContext, edge))
                    continue;
                reexport.push_back(edge.To());
            }
        } else if (*edge == EDT_OutTogetherBack) {
            auxOutputs.push_back(edge.To());
        } else if (*edge == EDT_Property) {
            TConstDepNodeRef prop = edge.To();
            if (prop->NodeType == EMNT_BuildCommand) {
                ui64 propId;
                TStringBuf propType, propValue;
                ParseCommandLikeProperty(TDepGraph::GetCmdName(prop).GetStr(), propId, propType, propValue);
                if (propType == NProps::DEPENDS)
                    depends.push_back(prop);
            }
        } else if (*edge == EDT_Group && IsPropertyTypeNode(edge.To()->NodeType)) {
            TConstDepNodeRef prop = edge.To();
            auto propName = TDepGraph::GetCmdName(prop).GetStr();
            if (propName == NStaticConf::MODULE_INPUTS_MARKER || propName == NStaticConf::INPUTS_MARKER)
                break;
        }
    }

    JsonWriter.BeginObject();
    {
        JsonWriter.WriteKey("id");
        JsonWriter.WriteString(MkComponentId(component, platform, Unhash));
        JsonWriter.WriteKey("kind");
        JsonWriter.WriteString("component");

        JsonWriter.WriteKey("type");
        JsonWriter.BeginObject();
        {
            JsonWriter.WriteKey("module");
            JsonWriter.WriteString(componentStruct.Get(NVariableDefs::VAR_MODULE_TYPE));
            JsonWriter.WriteKey("language");
            JsonWriter.WriteString(componentStruct.GetLang());
        }
        JsonWriter.EndObject();

        JsonWriter.WriteKey("input");
        JsonWriter.BeginObject();
        {
            if (!import.empty()) {
                JsonWriter.WriteKey("import");
                JsonWriter.BeginList();
                for (auto &ref : import)
                    JsonWriter.WriteString(MkComponentId(ref, platform, Unhash));
                JsonWriter.EndList();
            }

            if (!reexport.empty()) {
                JsonWriter.WriteKey("reexport");
                JsonWriter.BeginList();
                for (auto &ref : reexport)
                    JsonWriter.WriteString(MkComponentId(ref, platform, Unhash));
                JsonWriter.EndList();
            }

            auto doData = componentStruct.DataPaths && !componentStruct.DataPaths->empty();
            auto doDepends = !depends.empty();
            if (doData || doDepends) {
                JsonWriter.WriteKey("test");
                JsonWriter.BeginObject();
                if (doData) {
                    JsonWriter.WriteKey("data");
                    JsonWriter.BeginList();
                    for (auto &path : *componentStruct.DataPaths)
                        EmitPath(JsonWriter, path);
                    JsonWriter.EndList();
                }
                if (doDepends) {
                    JsonWriter.WriteKey("depends");
                    JsonWriter.BeginList();
                    for (const auto &prop : depends) {
                        for (const auto &dirEdge : prop.Edges()) {
                            if (IsPropToDirSearchDep(dirEdge)) {
                                for (const auto &modEdge : dirEdge.To().Edges()) {
                                    if (IsDirToModuleDep(modEdge)) {
                                        const TModule *mod = restoreContext.Modules.Get(AssumeFile(modEdge.To()->ElemId));
                                        if (mod && !mod->IsFakeModule() && (!mod->IsFromMultimodule() || mod->IsFinalTarget())) {
                                            JsonWriter.WriteString(MkComponentId(modEdge.To(), platform, Unhash));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    JsonWriter.EndList();
                }
                JsonWriter.EndObject();
            }
        }
        JsonWriter.EndObject();

        JsonWriter.WriteKey("builders");
        JsonWriter.BeginList();
        for (auto& builder : builders)
            JsonWriter.WriteString(MkBuilderId(builder, platform, Unhash));
        JsonWriter.EndList();

        JsonWriter.WriteKey("output");
        JsonWriter.BeginObject();
        {
            JsonWriter.WriteKey("main");
            JsonWriter.BeginList();
            EmitPath(JsonWriter, TDepGraph::GetFileName(component));
            JsonWriter.EndList();
            JsonWriter.WriteKey("auxiliary");
            JsonWriter.BeginList();
            for (auto &auxOutput : auxOutputs)
                EmitPath(JsonWriter, TDepGraph::GetFileName(auxOutput));
            JsonWriter.EndList();
        }
        JsonWriter.EndObject();
    }
    JsonWriter.EndObject();
    JsonWriter.Reset(ZeroState);
    JsonWriter.UnsafeWriteRawBytes("\n");
}

void TWriter::DumpBuilder(
    TConstDepNodeRef builder,
    TStringBuf platform,
    auto xidEval
) {
    TVector<TConstDepNodeRef> outputs;
    TVector<TConstDepNodeRef> implicitInputs;
    TVector<TConstDepNodeRef> explicitInputs;
    TVector<TConstDepNodeRef> tools;
    enum EInputSection {Skip, Implicit, Explicit};
    EInputSection inputSection = IsModuleType(builder->NodeType) ? EInputSection::Skip : EInputSection::Explicit;
    if (!IsActionMarker(builder)) {
        outputs.push_back(builder);
    }
    for (const auto& edge : builder.Edges()) {
        if (*edge == EDT_OutTogetherBack) {
            outputs.push_back(edge.To());
        }
        if (*edge == EDT_Group && IsPropertyTypeNode(edge.To()->NodeType)) {
            TConstDepNodeRef prop = edge.To();
            auto propName = TDepGraph::GetCmdName(prop).GetStr();
            if (propName == NStaticConf::MODULE_INPUTS_MARKER)
                inputSection = EInputSection::Implicit;
            if (propName == NStaticConf::INPUTS_MARKER)
                inputSection = EInputSection::Explicit;
        }
        if (*edge == EDT_BuildFrom) {
            switch (inputSection) {
            case EInputSection::Implicit:
                if (IsSrcFileType(edge.To()->NodeType)) // TODO can it be a module type?
                    implicitInputs.push_back(edge.To());
                break;
            case EInputSection::Explicit:
                if (IsSrcFileType(edge.To()->NodeType)) // TODO can it be a module type?
                    explicitInputs.push_back(edge.To());
                break;
            default:
                break;
            }
        }
        if (IsBuildCommandDep(edge)) {
            TConstDepNodeRef cmd = edge.To();
            for (const auto& cmdEdge : cmd.Edges()) {
                if (*cmdEdge == EDT_Include && IsModuleType(cmdEdge.To()->NodeType)) {
                    tools.push_back(cmdEdge.To());
                }
            }
        }
    }

    JsonWriter.BeginObject();
    {
        JsonWriter.WriteKey("id");
        JsonWriter.WriteString(MkBuilderId(builder, platform, Unhash));
        JsonWriter.WriteKey("kind");
        JsonWriter.WriteString("builder");
        JsonWriter.WriteKey("xid");
        JsonWriter.WriteString(xidEval(outputs));

        JsonWriter.WriteKey("input");
        JsonWriter.BeginObject();
        {
            if (!tools.empty()) {
                JsonWriter.WriteKey("tools");
                JsonWriter.BeginList();
                for (auto& tool : tools)
                    JsonWriter.WriteString(MkComponentId(tool, platform, Unhash));
                JsonWriter.EndList();
            }
            if (!implicitInputs.empty()) {
                JsonWriter.WriteKey("implicit");
                JsonWriter.BeginList();
                for (auto& input : implicitInputs)
                    EmitPath(JsonWriter, TDepGraph::GetFileName(input));
                JsonWriter.EndList();
            }
            if (!explicitInputs.empty()) {
                JsonWriter.WriteKey(IsModuleType(builder->NodeType) ? "explicit" : "artifacts");
                JsonWriter.BeginList();
                for (auto& input : explicitInputs)
                    EmitPath(JsonWriter, TDepGraph::GetFileName(input));
                JsonWriter.EndList();
            }
        }
        JsonWriter.EndObject();

        JsonWriter.WriteKey("output");
        JsonWriter.BeginObject();
        {
            JsonWriter.WriteKey("artifacts");
            JsonWriter.BeginList();
            for (auto &auxOutput : outputs)
                EmitPath(JsonWriter, TDepGraph::GetFileName(auxOutput));
            JsonWriter.EndList();
        }
        JsonWriter.EndObject();
    }
    JsonWriter.EndObject();
    JsonWriter.Reset(ZeroState);
    JsonWriter.UnsafeWriteRawBytes("\n");

}

void TWriter::DumpFile(
    TConstDepNodeRef file
) {
    TVector<TConstDepNodeRef> imports;
    for (const auto& edge : file.Edges())
        if (IsIncludeFileDep(edge) || IsMakeFileIncludeDep(edge.From()->NodeType, *edge, edge.To()->NodeType))
            imports.push_back(edge.To());
    JsonWriter.BeginObject();
    JsonWriter.WriteKey("kind");
    JsonWriter.WriteString("file");
    EmitPathKV(JsonWriter, TDepGraph::GetFileName(file));
    if (!imports.empty()) {
        JsonWriter.WriteKey("import");
        JsonWriter.BeginList();
        for (const auto& import : imports)
            EmitPath(JsonWriter, TDepGraph::GetFileName(import));
        JsonWriter.EndList();
    }
    JsonWriter.EndObject();
    JsonWriter.Reset(ZeroState);
    JsonWriter.UnsafeWriteRawBytes("\n");
}

//
//
//

struct TGraphExporterData {
    TVector<TConstDepNodeRef> Builders;
    bool WasFresh = false;
};

using TGraphExporterStateItem = TGraphIteratorStateItem<TGraphExporterData, true>;

class TGraphExporter: public TNoReentryStatsVisitor<TEntryStats, TGraphExporterStateItem> {
private:
    using TBase = TNoReentryStatsVisitor<TEntryStats, TGraphExporterStateItem>;

    const TRestoreContext RestoreContext_;
    const THashSet<TTarget>& StartTargets_;
    TWriter Formatter_;

    THashMap<ui32, TNodeId> DirElemId2ModuleNodeId_;

    TMakeModuleSequentialStates ModulesStatesCache_;
    const TCommands& Commands_;

    bool PrunePeerDirs_ = true;
    THashSet<TFileElemId> DumpedModuleDirs_;
    THashSet<TFileElemId> DumpedComponents_;

public:
    using typename TBase::TNodes;
    using TBase::Nodes;

    const TStringBuf Platform;
    const TVector<TString> Tags;
    const bool Unhash;

    TGraphExporter(TRestoreContext restoreContext,
                const THashSet<TTarget>& startTargets, IOutputStream& cmsg,
                const TBuildConfiguration& cf,
                const TCommands& commands)
        : RestoreContext_{restoreContext}
        , StartTargets_(startTargets)
        , Formatter_(cmsg, cf.DumpGraphStructuredFlagUnhash, cf.DumpGraphStructuredFlagPrettify)
        , ModulesStatesCache_(cf, restoreContext.Graph, restoreContext.Modules)
        , Commands_(commands)
        , PrunePeerDirs_(cf.DumpGraphStructuredFlagPrune)
        , Platform(cf.DumpGraphStructuredPlatform)
        , Tags(cf.DumpGraphStructuredTags)
        , Unhash(cf.DumpGraphStructuredFlagUnhash)
    {
    }

    void Epilogue();

    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);
};

bool TGraphExporter::AcceptDep(TState& state) {
    YDIAG(Iter)
        << "DUMP: accept? "
        << "[" << state.NextDep().From()->NodeType << "]"
        << RestoreContext_.Graph.ToString(state.NextDep().From())
        << " -" << state.NextDep().Value() << "-> "
        << "[" << state.NextDep().To()->NodeType << "]"
        << RestoreContext_.Graph.ToString(state.NextDep().To())
        << Endl;
    TBase::AcceptDep(state);
    const auto& dep = state.NextDep();
    if (IsPropertyDep(dep))
        return false;
    bool isStartModuleDep = !state.HasIncomingDep() && IsDirToModuleDep(dep);
    if (isStartModuleDep && !StartTargets_.contains(dep.To().Id()))
        return false;
    if (!IsReachableManagedDependency(RestoreContext_, dep))
        return false;
    if (PrunePeerDirs_ && IsPeerdirDep(dep))
        return false;
    return true;
}

bool TGraphExporter::Enter(TState& state) {
    auto& top = state.Top();
    bool fresh = top.WasFresh = TBase::Enter(state);
    YDIAG(Iter)
        << "DUMP: enter" << (fresh ? " " : "*") << "  "
        << "[" << state.Top().Node()->NodeType << "]"
        << state.Top().Print()
        << Endl;
    // TODO review and consider bringing over the skipping logic around TNodePrinter::SubGraphFilter
    return fresh;
}

class TKeyValueFormatter: public TJsonCmdAcceptor {
protected:
    void OnCmdFinished(const TVector<TSingleCmd>& commands[[maybe_unused]], TCommandInfo& cmdInfo, const TVars& vars[[maybe_unused]]) override {
        if (cmdInfo.KV)
            for (auto& kv : *cmdInfo.KV)
                KeyValue[kv.first] = kv.second;
    }

public:
    THashMap<TString, TString> KeyValue;
};

void TGraphExporter::Leave(TState& state) {
    YDIAG(Iter)
        << "DUMP: leave   "
        << "[" << state.Top().Node()->NodeType << "]"
        << state.Top().Print()
        << Endl;
    TBase::Leave(state);

    auto& top = state.Top();
    if (top.WasFresh) {
        const auto topNode = top.Node();
        const auto nodeType = topNode->NodeType;
        auto hasIncDep = state.HasIncomingDep();
        const auto incDep = state.IncomingDep();
        auto parent = state.Parent();

        if (IsFileType(topNode->NodeType) && !IsActionMarker(topNode)) {
            Formatter_.DumpFile(topNode);
        }
        if (IsBuilderNode(topNode)) {
            auto mod = state.FindRecent([](auto& item) {
                return IsModuleType(item.Node()->NodeType);
            });
            Formatter_.DumpBuilder(topNode, Platform, [&](auto& outputs) {
                if (mod == state.end())
                    return TString();

                // a crude (and partial) imitation of ya-bin's stats_uid computation

                TVector<TString> tags = Tags;
                TVector<TString> outputNames;
                THashMap<TString, TString> kvs;
                TStringStream ss;

                const auto& fileConf = RestoreContext_.Graph.Names().FileConf;
                for (auto& output: outputs) {
                    TFileView outputName = RestoreContext_.Graph.GetFileName(output);
                    TFileView resolvedName = fileConf.ResolveLink(outputName);
                    outputNames.push_back(RestoreContext_.Conf.RealPathEx(resolvedName));
                }
                std::sort(outputNames.begin(), outputNames.end());

                const TVars& cfg = RestoreContext_.Conf.CommandConf;
                auto build_type = TString(cfg.EvalValue("BUILD_TYPE"));
                build_type.to_lower();
                tags.push_back(build_type);
                tags.push_back(TString(Platform));
                std::sort(tags.begin(), tags.end());

                TKeyValueFormatter formatter{};
                TMakeCommand mkcmd{ModulesStatesCache_, RestoreContext_, Commands_, nullptr, &RestoreContext_.Conf.CommandConf};
                mkcmd.CmdInfo.MkCmdAcceptor = formatter.GetAcceptor();
                mkcmd.GetFromGraph(topNode.Id(), mod->Node().Id());
                kvs.swap(formatter.KeyValue);

                auto dump_single_quoted = [](TStringStream& ss, TStringBuf s) {ss << "'" << s << "'";};
                ss << "[";
                dump_single_quoted(ss, Platform);
                ss << ", \"[";
                for (auto& tag: tags) {
                    if (&tag != &*tags.begin())
                        ss << ", ";
                    dump_single_quoted(ss, tag);
                }
                ss << "]\", ";
                if (kvs.contains("p"))
                    dump_single_quoted(ss, kvs["p"]);
                else
                    dump_single_quoted(ss, "");
                ss << ", \"[";
                for (auto& output: outputNames) {
                    if (&output != &*outputNames.begin())
                        ss << ", ";
                    dump_single_quoted(ss, output);
                }
                ss << "]\"]";

                return Unhash ? ss.Str() : MkMD5(ss.Str());
            });
            if (mod != state.end())
                mod->Builders.push_back(topNode);
        }
        if (IsModuleType(nodeType)) {
            const TModule* mod = RestoreContext_.Modules.Get(AssumeFile(topNode->ElemId));
            Formatter_.DumpComponent(topNode, *mod, top.Builders, Platform, RestoreContext_);
            if (PrunePeerDirs_)
                DumpedComponents_.insert(AssumeFile(topNode->ElemId));
        }
        if (IsMakeFileType(nodeType) && hasIncDep && incDep.From()->NodeType == EMNT_Directory && *incDep == EDT_Include) {
            Y_ASSERT(parent != state.end());
            Formatter_.DumpModule(topNode, parent->Node(), nullptr, Platform);
            if (PrunePeerDirs_)
                DumpedModuleDirs_.insert(AssumeFile(parent->Node()->ElemId));
        }
    }
}

void TGraphExporter::Left(TState& state) {
    YDIAG(Iter)
        << "DUMP: left to "
        << "[" << state.Top().Node()->NodeType << "]"
        << state.Top().Print()
        << Endl;
    TBase::Left(state);
}

void TGraphExporter::Epilogue() {
    struct TModuleLeftovers {
        TFileElemId Makefile;
        THashSet<TFileElemId> Components;
    };

    std::map<TFileElemId, TModuleLeftovers> leftovers;

    for (const auto& component : DumpedComponents_) {
        auto comMod = RestoreContext_.Modules.Get(component);
        auto dirId = comMod->GetDirId();
        if (DumpedModuleDirs_.contains(dirId))
            continue;
        auto [it, added] = leftovers.insert({dirId, {}});
        if (added)
            it->second.Makefile = comMod->GetMakefileId();
        else
            Y_ASSERT(it->second.Makefile == comMod->GetMakefileId());
        it->second.Components.insert(component);
    }

    for (const auto& leftover : leftovers) {
        // ...->GetMakefileId() gets us the file itself, and we need a "link";
        // basically, see MakefileNodeNameForModule()
        auto mkfElemId = TFileId::CreateElemId(ELinkType::ELT_MKF, TFileId::Create(leftover.second.Makefile).GetTargetId());
        auto mkfNode = RestoreContext_.Graph.GetNodeById(EMNT_MakeFile, mkfElemId);
        auto dirNode = RestoreContext_.Graph.GetNodeById(EMNT_Directory, leftover.first);
        Formatter_.DumpFile(mkfNode);
        Formatter_.DumpModule(mkfNode, dirNode, &leftover.second.Components, Platform);
    }
}

}

//
//
//

void NStructuredJsonlGraph::ExportGraph(TYMake& yMake) {
    TGraphExporter printer(yMake.GetRestoreContext(), yMake.ModuleStartTargets, yMake.Conf.Cmsg(), yMake.Conf, yMake.Commands);

    // this section mirrors the respective setup in ExportJSON()
    // to support command rendering in builders' xid calculations;
    // if we ever get to removing that rendering, we should drop this part, too
    auto& conf = yMake.Conf;
    TFsPath sysSourceRoot = conf.SourceRoot;
    conf.SourceRoot = "$(SOURCE_ROOT)";
    TFsPath sysBuildRoot = conf.BuildRoot;
    conf.BuildRoot = "$(BUILD_ROOT)";
    const bool sysNormalize = conf.NormalizeRealPath;
    conf.NormalizeRealPath = true;
    conf.EnableRealPathCache(&yMake.Names.FileConf);
    Y_DEFER {
        conf.EnableRealPathCache(nullptr);
        conf.SourceRoot = sysSourceRoot;
        conf.BuildRoot = sysBuildRoot;
        conf.NormalizeRealPath = sysNormalize;
    };

    {
        THashSet<TNodeId> usedStartTargets;
        for (const auto& startTarget : yMake.StartTargets) {
            if (startTarget.IsModuleTarget) {
                continue;
            }
            if (usedStartTargets.contains(startTarget.Id)) {
                continue;
            }
            if (startTarget.IsNonDirTarget) {
                continue;
            }
            IterateAll(yMake.Graph, startTarget, printer);
            usedStartTargets.insert(startTarget.Id);
        }
        printer.Epilogue();
    }
}
