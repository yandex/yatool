#include "export_json.h"

#include "conf.h"
#include "dump_info.h"
#include "export_json_debug.h"
#include "json_subst.h"
#include "json_visitor.h"
#include "macro.h"
#include "make_plan_cache.h"
#include "mkcmd.h"
#include "saveload.h"
#include "vars.h"
#include "ymake.h"

#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/json_writer.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/progress_manager.h>
#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/make_plan/make_plan.h>
#include <devtools/ymake/mkcmd_inputs_outputs.h>
#include <devtools/ymake/symbols/symbols.h>

#include <library/cpp/blockcodecs/codecs.h>
#include <library/cpp/ucompress/writer.h>

#include <util/folder/path.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/buffered.h>
#include <util/stream/file.h>
#include <util/stream/output.h>
#include <util/system/types.h>

using EDebugUidType = NDebugEvents::NExportJson::EUidType;

namespace {
    TMd5Value ComputeUIDForPeersLateOutsNode(TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        // structure uid ноды
        TMd5Value md5{"ComputeUIDForPeersLateOutsNode::<md5>"sv};
        md5.Update(nodeInfo.NewUids()->GetStructureUid(), "ComputeUIDForPeersLateOutsNode::<StructureUid>"sv);

        // full uids of dependencies
        if (nodeInfo.NodeDeps) {
            for (const auto& dep : *nodeInfo.NodeDeps.Get()) {
                const auto depIt = cmdBuilder.Nodes.find(dep);
                Y_ASSERT(depIt != cmdBuilder.Nodes.end());

                const TMd5SigValue& depFullUid = depIt->second.NewUids()->GetFullUid();
                md5.Update(depFullUid, "ComputeUIDForPeersLateOutsNode::<depFullUid>"sv);
            }
        }
        return md5;
    }

    class TJSONRenderer {
    private:
        TDepGraph& Graph;
        TModules& Modules;
        const TBuildConfiguration& Conf;
        const TJSONEntryStats& NodeInfo;
        const TNodeId NodeId;
        const TNodeId ModuleId;
        TVector<std::pair<TStringBuf, TNodeId>> NodeDeps;
        TVector<std::pair<TStringBuf, TNodeId>> ToolDeps;

        TNodeDebugOnly NodeDebug;

        TMd5SigValue RenderId;

        TJSONVisitor& CmdBuilder;

        TDumpInfoUID DumpInfo;
        TSubst2Json Subst2Json;
        TMakeCommand MakeCommand;

        bool RenderedWithoutSubst = false;
        bool IsGlobalNode = false;

    public:
        TJSONRenderer(TYMake& ymake, TJSONVisitor& cmdBuilder, const TNodeId& nodeId, const TJSONEntryStats& nodeInfo, TMakeNode* resultNode)
                : Graph(ymake.Graph)
                , Modules(ymake.Modules)
                , Conf(ymake.Conf)
                , NodeInfo(nodeInfo)
                , NodeId(nodeId)
                , ModuleId(cmdBuilder.GetModuleByNode(nodeId))
                , NodeDebug(Graph, nodeId)
                , RenderId{NodeDebug, "TJSONRenderer::RenderId"sv}
                , CmdBuilder(cmdBuilder)
                , DumpInfo(nodeInfo.GetNodeUid(), nodeInfo.GetNodeSelfUid())
                , Subst2Json(cmdBuilder, DumpInfo, resultNode), MakeCommand(ymake)
        {
            PrepareDeps();

            const TModule* mod = GetModule();
            IsGlobalNode = mod != nullptr && mod->GetGlobalLibId() == Graph[NodeId]->ElemId;
        }

        void RenderNodeDelayed() {
            PrepareNodeForRendering();
            Y_ASSERT(NodeId != 0);
            MakeCommand.GetFromGraph(NodeId, ModuleId, ECF_Json, &DumpInfo, true, IsGlobalNode);
            RenderedWithoutSubst = true;
        }

        void CompleteRendering() {
            Y_ASSERT(RenderedWithoutSubst);
            MakeCommand.RenderCmdStr(ECF_Json, &CmdBuilder.ErrorShower);
            RenderedWithoutSubst = false;
        }

        TString CalculateRenderId() {
            Y_ASSERT(RenderedWithoutSubst);

            TMd5Value md5{NodeDebug, "TJSONRenderer::CalculateRenderId::<md5>"sv};
            md5.Update(RenderId, "TJSONRenderer::CalculateRenderId::<RenderId>"sv);

            TVector<std::pair<TStringBuf, const TYVar*>> vars;
            vars.reserve(MakeCommand.Vars.size());
            for (const auto& varIt : MakeCommand.Vars) {
                vars.push_back(std::make_pair(TStringBuf(varIt.first), &varIt.second));
            }
            Sort(vars);

            for (const auto& [name, var] : vars) {
                if (name == "UID") {
                    continue;
                }
                md5.Update(name, "TJSONRenderer::CalculateRenderId::<name>"sv);
                md5.Update(&(var->Flags), sizeof(var->Flags), "TJSONRenderer::CalculateRenderId::<var->Flags>"sv);
                bool needExpandVar = !IsInternalReservedVar(name) && !Conf.CommandConf.IsReservedName(name);
                for (const auto& varStr : *var) {
                    TStringBuf value = varStr.Name;
                    if (needExpandVar) {
                        value = GetCmdValue(value);
                    }
                    md5.Update(value, "TJSONRenderer::CalculateRenderId::<value>"sv);
                    md5.Update(&varStr.AllFlags, sizeof(varStr.AllFlags), "TJSONRenderer::CalculateRenderId::<varStr.AllFlags>"sv);
                }
            }

            TMd5SigValue sign{NodeDebug, "TJSONRenderer::CalculateRenderId::<sign>"sv};
            sign.MoveFrom(std::move(md5));
            return sign.ToBase64();
        };

        TString CalculateCacheUid() {
            const TModule* mod = GetModule();
            if (mod && ModuleId == NodeId && mod->GetAttrs().UsePeersLateOuts) {
                auto uid = ComputeUIDForPeersLateOutsNode(CmdBuilder, NodeId).ToBase64();
                return uid;
            }
            return NodeInfo.NewUids()->GetStructureUid().ToBase64();
        }

        void RefreshEmptyMakeNode(TMakeNode& node, const TMakeNodeSavedState& nodeSavedState, NCache::TConversionContext& context) {
            node.Uid = DumpInfo.UID;
            node.SelfUid = DumpInfo.SelfUID;

            FillNodeDeps(node);
            ReplaceNodeInputs();
            RestoreLateOutsFromNode(nodeSavedState, context);
        }

        void RefreshPartiallyRestoredNode(TMakeNode& node) {
            node.Deps.clear();
            node.Inputs.clear();
            node.ToolDeps.clear();
            ReplaceNodeInputs();
            Subst2Json.FakeFinish(MakeCommand.CmdInfo);
        }

        void RestoreLateOutsFromNode(const TMakeNodeSavedState& nodeSavedState, NCache::TConversionContext& context) {
            if (NodeId != ModuleId) {
                return;
            }

            ui32 moduleElemId = Graph[ModuleId]->ElemId;
            auto& moduleLateOuts = Modules.GetModuleLateOuts(moduleElemId);
            moduleLateOuts = nodeSavedState.RestoreLateOuts(context);
        }

    private:
        void PrepareDeps() {
            if (NodeInfo.NodeDeps.Get()) {
                NodeDeps.reserve(NodeInfo.NodeDeps.Get()->size());
                for (const auto& depId : *NodeInfo.NodeDeps.Get()) {
                    TFileView name = Graph.GetFileName(Graph.Get(depId));
                    Y_ASSERT(!name.IsLink());
                    NodeDeps.push_back({name.GetTargetStr(), depId});
                }
                Sort(NodeDeps);
            }
            if (NodeInfo.NodeToolDeps.Get()) {
                ToolDeps.reserve(NodeInfo.NodeToolDeps.Get()->size());
                for (const auto& depId : *NodeInfo.NodeToolDeps.Get()) {
                    TFileView name = Graph.GetFileName(Graph.Get(depId));
                    Y_ASSERT(!name.IsLink());
                    ToolDeps.push_back({name.GetTargetStr(), depId});
                }
                Sort(ToolDeps);
            }

        }

        void FillNodeDeps(TMakeNode& node) const {
            if (!NodeDeps.empty()) {
                node.Deps.reserve(NodeDeps.size());
                for (const auto& [_, depId] : NodeDeps) {
                    const auto dependency = CmdBuilder.Nodes.find(depId);
                    Y_ASSERT(dependency != CmdBuilder.Nodes.end());
                    if (dependency->second.HasBuildCmd) {
                        node.Deps.emplace_back(dependency->second.GetNodeUid());
                    }
                }
            }
            if (!ToolDeps.empty()) {
                node.ToolDeps.reserve(ToolDeps.size());
                for (const auto& [name, depId] : ToolDeps) {
                    const auto dependency = CmdBuilder.Nodes.find(depId);
                    Y_ASSERT(dependency != CmdBuilder.Nodes.end());
                    Y_ASSERT(dependency->second.HasBuildCmd);
                    node.ToolDeps.emplace_back(dependency->second.GetNodeUid());
                }
            }
        }

        void ReplaceNodeInputs() {
            if (!Conf.StoreInputsInJsonCache) {
                FillRegularInputs();
                FillFullInputs();
                PrepareInputs();

                Subst2Json.UpdateInputs();
            }
        }

        TMd5Value CalculateDepsId() {
            TMd5Value md5{TNodeDebugOnly{Graph, NodeId}, "TJSONRenderer::CalculateDepsId::<md5>"sv};
            for (const auto& [depName, _] : NodeDeps) {
                md5.Update(depName, "TJSONRenderer::CalculateDepsId::<depName>"sv);
            }
            // Note: we rely here on the fact that tools are also included into deps
            return md5;
        }

        void PrepareNodeForRendering() {
            Y_ASSERT(!RenderedWithoutSubst);

            FillDeps();
            FillRegularInputs();
            if (Conf.DumpInputsInJSON) {
                FillFullInputs();
            }
            PrepareInputs();

            FillExtraOuts();

            Subst2Json.GenerateJsonTargetProperties(Graph[NodeId], GetModule(), IsGlobalNode);
            MakeCommand.CmdInfo.MkCmdAcceptor = Subst2Json.GetAcceptor();
        }

        void FillFullInputs() {
            const auto& nodeInputs = CmdBuilder.GetNodeInputs(NodeId);
            if (nodeInputs && !nodeInputs->empty()) {
                for (const auto& input : *nodeInputs.Get()) {
                    DumpInfo.Inputs().push_back(Conf.RealPath(Graph.GetFileName(Graph.Get(input)))); // FIXME(spreis): This is really big hammer
                }
            }
        }

        void FillDeps() {
            for (const auto& [depName, depId] : NodeDeps) {
                const auto dependencyIt = CmdBuilder.Nodes.find(depId);
                Y_ASSERT(dependencyIt != CmdBuilder.Nodes.end());
                if (dependencyIt->second.HasBuildCmd) {
                    DumpInfo.Deps.Push(depId);
                }
            }

            for (const auto& [depName, depId] : ToolDeps) {
                const auto dependencyIt = CmdBuilder.Nodes.find(depId);
                Y_ASSERT(dependencyIt != CmdBuilder.Nodes.end());
                Y_ASSERT(dependencyIt->second.HasBuildCmd);
                DumpInfo.ToolDeps.Push(depId);
                // Note: we rely here on the fact that tools are also included into deps
            }
        }

        void PrepareInputs() {
            THashSet<TString> uniqInputs;
            uniqInputs.reserve(DumpInfo.Inputs().size());
            TVector<TString> preparedInputs{Reserve(DumpInfo.Inputs().size())};

            for (const TString& input : DumpInfo.Inputs()) {
                auto [_, wasNew] = uniqInputs.insert(input);
                if (wasNew) {
                    preparedInputs.push_back(input);
                }
            }

            DumpInfo.Inputs().swap(preparedInputs);
        }

        void FillRegularInputs() {
            TString moduleDir;
            auto getModuleDir = [&]() -> TStringBuf {
                if (moduleDir.Empty()) {
                    const TFileView moduleFile = Graph.GetFileName(Graph.Get(ModuleId));
                    moduleDir = NPath::SetType(NPath::Parent(moduleFile.GetTargetStr()), NPath::Source);
                }
                return moduleDir;
            };

            for (const auto& [_, depId] : NodeDeps) {
                const auto depNodeRef = Graph[depId];

                if (IsModuleType(depNodeRef->NodeType) && !Conf.DumpInputsInJSON && !Conf.ShouldAddPeersToInputs()) {
                    continue;
                }

                DumpInfo.Inputs().push_back(InputToPath(Conf, depNodeRef, getModuleDir));
            }

            auto isGlobalSrc = [](const TConstDepNodeRef&) {
                // We assume there is no GlobalSrcs in explicit inputs,
                // and we running ProcessInputsAndOutputs here only for them.
                // There is already assert for this in ProcessInputsAndOutputs
                // when doing full processing from TMakeCommand::MineInputsAndOutputs.
                return false;
            };

            auto addInput = [&](const TConstDepNodeRef& inputNode, bool /* explicitInputs */) {
                DumpInfo.Inputs().push_back(InputToPath(Conf, inputNode, getModuleDir));
            };

            bool isModule = NodeId == ModuleId;
            ProcessInputsAndOutputs<true>(Graph[NodeId], isModule, Modules, addInput, isGlobalSrc);
        }

        void FillExtraOuts() {
            if (!NodeInfo.ExtraOuts.Get()) {
                return;
            }

            for (const auto& extraOutput : *NodeInfo.ExtraOuts.Get()) {
                const TString extraOutputPath = Conf.RealPath(Graph.GetFileName(Graph.Get(extraOutput)));
                if (!extraOutputPath.empty()) {
                    DumpInfo.ExtraOutput.push_back(TVarStr(extraOutputPath, false, true));
                }
            }
        }

        const TModule* GetModule() const {
            const auto moduleNode = Graph[ModuleId];
            return Modules.Get(moduleNode->ElemId);
        }
    };

    TString RenderMakeNode(const TMakeNode& node) {
        TStringStream out;
        NYMake::TJsonWriter jsonWriter(out);
        node.WriteAsJson(jsonWriter);
        jsonWriter.Flush();
        return out.Str();
    };

    bool RestoreJsonNodeFromCache(TYMake& yMake, TJSONVisitor& cmdbuilder, TMakePlanCache& cache, const TNodeId nodeId, const TJSONEntryStats& nodeInfo, TMakeNode* node) {
        TJSONRenderer renderer(yMake, cmdbuilder, nodeId, nodeInfo, node);

        const auto cacheUid = renderer.CalculateCacheUid();
        return cache.RestoreByCacheUid(cacheUid, node);
    }

    TString RestoreJsonNodeFromCacheString(TYMake& yMake, TJSONVisitor& cmdbuilder, TMakePlanCache& cache, const TNodeId nodeId, const TJSONEntryStats& nodeInfo) {
        TMakeNode makeNode;
        if (!RestoreJsonNodeFromCache(yMake, cmdbuilder, cache, nodeId, nodeInfo, &makeNode))
            return TString{};
        return RenderMakeNode(makeNode);
    }

    void RenderJsonNodeFull(TYMake& yMake, TJSONVisitor& cmdbuilder, const TNodeId nodeId, const TJSONEntryStats& nodeInfo, TMakeNode* node) {
        TJSONRenderer renderer(yMake, cmdbuilder, nodeId, nodeInfo, node);
        renderer.RenderNodeDelayed();
        renderer.CompleteRendering();
    }

    TString RenderJsonNodeFullString(TYMake& yMake, TJSONVisitor& cmdbuilder, const TNodeId nodeId, const TJSONEntryStats& nodeInfo) {
        TMakeNode makeNode;
        RenderJsonNodeFull(yMake, cmdbuilder, nodeId, nodeInfo, &makeNode);
        return RenderMakeNode(makeNode);
    }

    void RenderOrRestoreJSONNodeImpl(TYMake& yMake, TJSONVisitor& cmdbuilder, TMakePlan& plan, TMakePlanCache& cache, const TNodeId nodeId, const TJSONEntryStats& nodeInfo, NYMake::TJsonWriter& jsonWriter) {
        TMakeNode node;
        TJSONRenderer renderer(yMake, cmdbuilder, nodeId, nodeInfo, &node);

        if (!yMake.Conf.ReadJsonCache && !yMake.Conf.WriteJsonCache) {
            renderer.RenderNodeDelayed();
            renderer.CompleteRendering();
            jsonWriter.WriteArrayValue(plan.NodesArr, node, nullptr);
            return;
        }

        const auto cacheUid = renderer.CalculateCacheUid();
        const auto* cachedNode = cache.GetCachedNodeByCacheUid(cacheUid);
        BINARY_LOG(UIDs, NExportJson::TCacheSearch, yMake.Graph, nodeId, EDebugUidType::Cache, cacheUid, static_cast<bool>(cachedNode));
        if (cachedNode) {
            cache.Stats.Inc(NStats::EJsonCacheStats::NoRendered);
            auto& context = cache.GetConversionContext(&node);
            renderer.RefreshEmptyMakeNode(node, *cachedNode, context);
            jsonWriter.WriteArrayValue(plan.NodesArr,*cachedNode, &context);
            return;
        }

        // If we didn't find exactly the same node, try to find the same, but with different UID.
        // To make it possible, use partial rendering and calculation of RenderId.

        renderer.RenderNodeDelayed();
        renderer.CompleteRendering();

        const auto nodeName = yMake.Graph.ToTargetStringBuf(nodeId);
        cache.AddRenderedNode(node, nodeName, cacheUid, TString{});
        cache.Stats.Inc(NStats::EJsonCacheStats::FullyRendered);
        jsonWriter.WriteArrayValue(plan.NodesArr, node, nullptr);
    }

    void RenderOrRestoreJSONNode(TYMake& yMake, TJSONVisitor& cmdbuilder, TMakePlan& plan, TMakePlanCache& cache, const TNodeId nodeId, const TJSONEntryStats& nodeInfo, NYMake::TJsonWriter& jsonWriter) {
        try {
            RenderOrRestoreJSONNodeImpl(yMake, cmdbuilder, plan, cache, nodeId, nodeInfo, jsonWriter);
#if defined(NEW_UID_COMPARE)
            TString currentRender = RenderMakeNode(plan.Nodes.back());
            TString fullRender = RenderJsonNodeFullString(yMake, cmdbuilder, nodeId, nodeInfo);
            if (currentRender != fullRender) {
                Cerr << "JSON renders differ\n";
                Cerr << "Actual: " << currentRender << '\n';
                Cerr << "Expected: " << fullRender << '\n';
            }
#endif
        } catch (const std::exception& renderError) {
#if !defined (YMAKE_DEBUG)
            Y_UNUSED(renderError, RenderJsonNodeFullString, RestoreJsonNodeFromCacheString);
            throw;
#else
            Cerr << "JSON rendering failed: " << renderError.what() << '\n';

            try {
                TString fullRender = RenderJsonNodeFullString(yMake, cmdbuilder, nodeId, nodeInfo);
                Cerr << "Full JSON render: " << fullRender << '\n';
            } catch (const std::exception& fullRenderError) {
                Cerr << "Full JSON rendering failed: " << fullRenderError.what() << '\n';
            }

            try {
                TString fromCache = RestoreJsonNodeFromCacheString(yMake, cmdbuilder, cache, nodeId, nodeInfo);
                if (!fromCache.Empty())
                    Cerr << "JSON node in cache: " << fromCache << '\n';
            } catch (const std::exception& restoreError) {
                Cerr << "JSON restore from cache failed: " << restoreError.what() << '\n';
            }

            throw;
#endif
        }
    }

    void ComputeFullUID(TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        if (nodeInfo.NewUids()->IsFullUidCompleted()) {
            return;
        }

        // full uids of dependencies
        if (nodeInfo.NodeDeps) {
            // self uid ноды
            TMd5Value md5{"ComputeFullUID::<md5>"sv};
            md5.Update(nodeInfo.NewUids()->GetSelfUid(), "ComputeFullUID::<SelfUid>"sv);

            for (const auto& dep : *nodeInfo.NodeDeps.Get()) {
                const auto depIt = cmdBuilder.Nodes.find(dep);
                Y_ASSERT(depIt != cmdBuilder.Nodes.end());

                const TJSONEntryStats& depData = depIt->second;

                if (!depData.NewUids()->IsFullUidCompleted()) {
                    ComputeFullUID(cmdBuilder, dep);
                }
                const TMd5SigValue& depFullUid = depData.NewUids()->GetFullUid();
                md5.Update(depFullUid, "ComputeFullUID::<depFullUid>"sv);
            }

            nodeInfo.NewUids()->SetFullUid(md5);

        } else {
            nodeInfo.NewUids()->SetFullUid(nodeInfo.NewUids()->GetSelfUid());
        }

    }

    void ComputeSelfUID(TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        if (nodeInfo.NewUids()->IsSelfUidCompleted()) {
            return;
        }
        // структурный uid главного output'a
        TMd5Value md5{"ComputeSelfUID::<md5>"sv};
        md5.Update(nodeInfo.NewUids()->GetStructureUid(), "ComputeSelfUID::<StructureUid>"sv);
        md5.Update(nodeInfo.NewUids()->GetContentUid(), "ComputeSelfUID::<ContentUid>"sv);

        nodeInfo.NewUids()->SetSelfUid(md5);
    }

    void UpdateUids(TJSONVisitor& cmdbuilder, TNodeId nodeId) {
        ComputeSelfUID(cmdbuilder, nodeId);
        ComputeFullUID(cmdbuilder, nodeId);
    }

    void RenderJSONGraph(TYMake& yMake, TMakePlan& plan) {
        auto& graph = yMake.Graph;
        const auto& conf = yMake.Conf;
        const auto& startTargets = yMake.StartTargets;

        YDebug() << "Exporting JSON" << Endl;

        FORCE_TRACE(U, NEvent::TStageStarted("Visit JSON"));
        THolder<TJSONVisitor> cmdbuilderHolder;
        auto cmdBuilderReset = [&]() {
            cmdbuilderHolder.Reset(new TJSONVisitor(yMake.GetRestoreContext(), yMake.Commands, graph.Names().CommandConf, startTargets));
        };

        cmdBuilderReset();

        try {
            yMake.LoadUids(cmdbuilderHolder.Get());
        } catch (const std::exception& e) {
            YDebug() << "Uids cache failed to be loaded: " << e.what() << Endl;
            // JSON visitor can be left in an incorrect state
            // after an unsuccessful cache loading.
            cmdBuilderReset();
        }

        TJSONVisitor& cmdbuilder = *cmdbuilderHolder.Get();

        IterateAll(graph, startTargets, cmdbuilder, [](const TTarget& t) -> bool {
            return !t.IsNonDirTarget && !t.IsModuleTarget;
        });
        FORCE_TRACE(U, NEvent::TStageFinished("Visit JSON"));

        plan.Resources = cmdbuilder.GetResources();
        plan.HostResources = cmdbuilder.GetHostResources();
        if (conf.DumpInputsMapInJSON) {
            plan.Inputs = cmdbuilder.GetInputs(graph);
        }

        TProgressManager::Instance()->ForceUpdateRenderModulesTotal(cmdbuilder.GetModuleNodesNum());

        THashSet<TNodeId> results;
        for (const auto& startTarget : startTargets) {
            if (conf.DependsLikeRecurse || !startTarget.IsDependsTarget || startTarget.IsRecurseTarget) {
                TVector<TNodeId> moduleIds, glSrcIds;
                yMake.ListTargetResults(startTarget, moduleIds, glSrcIds);

                for (TNodeId moduleId : moduleIds) {
                    const TModule* mod = yMake.Modules.Get(graph[moduleId]->ElemId);
                    if (mod != nullptr && mod->GetAttrs().UseInjectedData) {
                        YDebug() << "JSON: name " << graph.GetFileName(yMake.Graph.Get(moduleId)) << " will be injected in graph later. Noop here." << Endl;
                        continue;
                    }
                    if (mod != nullptr && mod->IsFakeModule()) {
                        YDIAG(V) << "JSON: name " << graph.GetFileName(yMake.Graph.Get(moduleId)) << " is fake, excluded" << Endl;
                        continue;
                    }

                    if (cmdbuilder.Nodes.contains(moduleId)) {
                        results.insert(moduleId);
                    }
                }
                for (TNodeId glSrcId : glSrcIds) {
                    if (cmdbuilder.Nodes.contains(glSrcId)) {
                        results.insert(glSrcId);
                    }
                }
            }
        }

        TFsPath tmpFile;
        TFsPath cacheFile;
        {
            YDebug() << "Store inputs in JSON cache: " << (yMake.Conf.StoreInputsInJsonCache ? "enabled" : "disabled") << '\n';

            TMakePlanCache cache(yMake.Conf);
            yMake.JSONCacheLoaded(cache.LoadFromFile());
            plan.WriteConf();

            for (const auto& nodeId: cmdbuilder.GetOrderedNodes()) {
                UpdateUids(cmdbuilder, nodeId);

                const auto& node = cmdbuilder.Nodes.at(nodeId);
                RenderOrRestoreJSONNode(yMake, cmdbuilder, plan, cache, nodeId, node, plan.Writer);
                if (IsModuleType(graph[nodeId]->NodeType)) {
                    TProgressManager::Instance()->IncRenderModulesDone();
                }
            }

            TProgressManager::Instance()->ForceRenderModulesDone();
            tmpFile = cache.SaveToFile();
            cacheFile = cache.GetCachePath();
            YDebug() << cache.GetStatistics() << Endl;
        }
        // Release cache before rename: on Windows open cache file prevents renaming
        if (tmpFile && cacheFile) {
            tmpFile.RenameTo(cacheFile);
        }

        // after rendering all nodes
        for (TNodeId result : results) {
            auto resultIt = cmdbuilder.Nodes.find(result);
            if (resultIt->second.OutTogetherDependency != 0 && !resultIt->second.HasBuildCmd) {
                resultIt = cmdbuilder.Nodes.find(resultIt->second.OutTogetherDependency);
            }
            plan.Results.push_back(resultIt->second.GetNodeUid());
        }
        std::sort(plan.Results.begin(), plan.Results.end());

        yMake.SaveUids(&cmdbuilder);
        cmdbuilder.ReportCacheStats();
        if (cmdbuilder.ErrorShower.Count != 0) {
            YErr() << "Expression errors found: " << cmdbuilder.ErrorShower.Count << Endl;
        }
    }

    class TOutputStreamWrapper {
    public:
        TOutputStreamWrapper(const TString& outFile, const TString& compressionCodec) {
            if (outFile == "-") {
                OutputStreamPtr_ = &Cout;
            } else {
                OutputStreamHolder_ = MakeHolder<TUnbufferedFileOutput>(outFile);
                OutputStreamPtr_ = OutputStreamHolder_.Get();
            }

            if (compressionCodec) {
                CodecOutputPtr_ = MakeHolder<NUCompress::TCodedOutput>(OutputStreamPtr_, NBlockCodecs::Codec(compressionCodec));
                OutputStreamPtr_ = CodecOutputPtr_.Get();
            }
        }

        IOutputStream* Get() {
            return OutputStreamPtr_;
        }

    private:
        IOutputStream* OutputStreamPtr_;
        THolder<IOutputStream> OutputStreamHolder_;
        THolder<NUCompress::TCodedOutput> CodecOutputPtr_;
    };

}

void ExportJSON(TYMake& yMake) {
    FORCE_TRACE(U, NEvent::TStageStarted("Export JSON"));

    auto& conf = yMake.Conf;
    TFsPath sysSourceRoot = conf.SourceRoot;
    conf.SourceRoot = "$(SOURCE_ROOT)";
    TFsPath sysBuildRoot = conf.BuildRoot;
    conf.BuildRoot = "$(BUILD_ROOT)";
    const bool sysNormalize = conf.NormalizeRealPath;
    conf.NormalizeRealPath = true;
    conf.EnableRealPathCache(&yMake.Names.FileConf);

    {
        NYMake::TTraceStageWithTimer writeJsonTimer("Write JSON", MON_NAME(EYmakeStats::WriteJSONTime));

        TOutputStreamWrapper output{conf.WriteJSON, conf.JsonCompressionCodec};
        NYMake::TJsonWriter jsonWriter(*output.Get());
        TMakePlan plan(jsonWriter);

        NYMake::TTraceStageWithTimer renderJsonTimer("Render JSON", MON_NAME(EYmakeStats::RenderJSONTime));
        RenderJSONGraph(yMake, plan);
    }

    conf.EnableRealPathCache(nullptr);
    conf.SourceRoot = sysSourceRoot;
    conf.BuildRoot = sysBuildRoot;
    conf.NormalizeRealPath = sysNormalize;

    FORCE_TRACE(U, NEvent::TStageFinished("Export JSON"));

    yMake.JSONCacheMonEvent();
    yMake.UidsCacheMonEvent();
}
