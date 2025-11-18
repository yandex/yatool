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

#include <asio/detached.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <devtools/ymake/action.h>
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

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

using EDebugUidType = NDebugEvents::NExportJson::EUidType;

namespace {
    TMd5Value ComputeUIDForPeersLateOutsNode(TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        // structure uid ноды
        TMd5Value md5{"ComputeUIDForPeersLateOutsNode::<md5>"sv};
        md5.Update(nodeInfo.GetStructureUid(), "ComputeUIDForPeersLateOutsNode::<StructureUid>"sv);

        // full uids of dependencies
        if (nodeInfo.NodeDeps) {
            for (const auto& dep : *nodeInfo.NodeDeps.Get()) {
                const auto depIt = cmdBuilder.Nodes.find(dep);
                Y_ASSERT(depIt != cmdBuilder.Nodes.end());

                const TMd5SigValue& depFullUid = depIt->second.GetFullUid();
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
        TJSONRenderer(TYMake& ymake, TJSONVisitor& cmdBuilder, const TNodeId& nodeId, const TJSONEntryStats& nodeInfo, TMakeNode* resultNode, TMakeModuleStates& modulesStatesCache)
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
                , Subst2Json(cmdBuilder, DumpInfo, resultNode, ymake.Conf.FillModule2Nodes, Modules.Get(Graph[ModuleId]->ElemId))
                , MakeCommand(modulesStatesCache, ymake)
        {
            PrepareDeps();

            const TModule* mod = GetModule();
            IsGlobalNode = mod != nullptr && mod->GetGlobalLibId() == Graph[NodeId]->ElemId;
        }

        void RenderNodeDelayed() {
            PrepareNodeForRendering();
            Y_ASSERT(NodeId != TNodeId::Invalid);
            MakeCommand.GetFromGraph(NodeId, ModuleId, &DumpInfo, true, IsGlobalNode);
            RenderedWithoutSubst = true;
        }

        void CompleteRendering() {
            Y_ASSERT(RenderedWithoutSubst);
            MakeCommand.RenderCmdStr(&CmdBuilder.ErrorShower);
            RenderedWithoutSubst = false;
        }

        TString CalculateCacheUid() {
            const TModule* mod = GetModule();
            if (mod && ModuleId == NodeId && mod->GetAttrs().UsePeersLateOuts) {
                auto uid = ComputeUIDForPeersLateOutsNode(CmdBuilder, NodeId).ToBase64();
                return uid;
            }
            return NodeInfo.GetStructureUid().ToBase64();
        }

        void RefreshEmptyMakeNode(TMakeNode& node, const TMakeNodeSavedState& nodeSavedState, NCache::TConversionContext& context) {
            node.Uid = DumpInfo.UID;
            node.SelfUid = DumpInfo.SelfUID;

            FillNodeDeps(node);
            ReplaceNodeInputs();
            RestoreLateOutsFromNode(nodeSavedState, context);
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
                    Y_ASSERT(!name.IsLink() || name.GetContextType() == ELT_Action);
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
                if (moduleDir.empty()) {
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

    void RenderOrRestoreJSONNode(TYMake& yMake, TJSONVisitor& cmdbuilder, NYMake::TJsonWriter::TOpenedArray& nodesArr, TMakePlanCache& cache, const TNodeId nodeId, const TJSONEntryStats& nodeInfo, NYMake::TJsonWriter& jsonWriter, TMakeModuleStates& modulesStatesCache) {
        TMakeNode node;
        TJSONRenderer renderer(yMake, cmdbuilder, nodeId, nodeInfo, &node, modulesStatesCache);

        if (!yMake.Conf.ReadJsonCache && !yMake.Conf.WriteJsonCache) {
            renderer.RenderNodeDelayed();
            renderer.CompleteRendering();
            jsonWriter.WriteArrayValue(nodesArr, node, nullptr);
            return;
        }

        const auto cacheUid = renderer.CalculateCacheUid();
        {
            const auto* cachedNode = cache.GetCachedNodeByCacheUid(cacheUid);
            BINARY_LOG(UIDs, NExportJson::TCacheSearch, yMake.Graph, nodeId, EDebugUidType::Cache, cacheUid, static_cast<bool>(cachedNode));
            if (cachedNode) {
                cache.Stats.Inc(NStats::EJsonCacheStats::NoRendered);
                auto context = cache.GetConstConversionContext(&node);
                renderer.RefreshEmptyMakeNode(node, *cachedNode, context);
                jsonWriter.WriteArrayValue(nodesArr, *cachedNode, &context);
                return;
            }
        }

        // If we didn't find exactly the same node, try to find the same, but with different UID.
        // To make it possible, use partial rendering and calculation of RenderId.

        renderer.RenderNodeDelayed();
        renderer.CompleteRendering();

        const auto nodeName = yMake.Graph.ToTargetStringBuf(nodeId);
        cache.AddRenderedNode(node, nodeName, cacheUid, TString{});
        cache.Stats.Inc(NStats::EJsonCacheStats::FullyRendered);
        jsonWriter.WriteArrayValue(nodesArr, node, nullptr);
    }

    void ComputeFullUID(const TDepGraph& graph, TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        if (nodeInfo.IsFullUidCompleted()) {
            return;
        }

        // full uids of dependencies
        if (nodeInfo.NodeDeps) {
            // self uid ноды
            TMd5Value md5{"ComputeFullUID::<md5>"sv};
            md5.Update(nodeInfo.GetSelfUid(), "ComputeFullUID::<SelfUid>"sv);

            for (const auto& dep : *nodeInfo.NodeDeps.Get()) {
                const auto depIt = cmdBuilder.Nodes.find(dep);
                Y_ASSERT(depIt != cmdBuilder.Nodes.end());

                TNodeId depNodeId = depIt->first;
                const TJSONEntryStats& depData = depIt->second;

                auto isBuildDep = [&]() {
                    if (depData.HasBuildCmd)
                        return true;
                    if (depData.OutTogetherDependency == TNodeId::Invalid)
                        return false;
                    return !IsMainOutput(graph, depNodeId, depData.OutTogetherDependency);
                };

                if (!isBuildDep())
                    continue;

                if (!depData.IsFullUidCompleted()) {
                    ComputeFullUID(graph, cmdBuilder, dep);
                }
                const TMd5SigValue& depFullUid = depData.GetFullUid();
                md5.Update(depFullUid, "ComputeFullUID::<depFullUid>"sv);
            }

            nodeInfo.SetFullUid(md5);

        } else {
            nodeInfo.SetFullUid(nodeInfo.GetSelfUid());
        }

    }

    void ComputeSelfUID(TJSONVisitor& cmdBuilder, TNodeId nodeId) {
        auto& nodeInfo = cmdBuilder.Nodes.at(nodeId);
        if (nodeInfo.IsSelfUidCompleted()) {
            return;
        }
        // структурный uid главного output'a
        TMd5Value md5{"ComputeSelfUID::<md5>"sv};
        md5.Update(nodeInfo.GetStructureUid(), "ComputeSelfUID::<StructureUid>"sv);
        md5.Update(nodeInfo.GetContentUid(), "ComputeSelfUID::<ContentUid>"sv);

        nodeInfo.SetSelfUid(md5);
    }

    void UpdateUids(const TDepGraph& graph, TJSONVisitor& cmdbuilder, TNodeId nodeId) {
        ComputeSelfUID(cmdbuilder, nodeId);
        ComputeFullUID(graph, cmdbuilder, nodeId);
    }

    asio::awaitable<void> RenderJSONGraph(TYMake& yMake, TMakePlan& plan, asio::any_io_executor exec) {
        auto& graph = yMake.Graph;
        const auto& conf = yMake.Conf;
        const auto& startTargets = yMake.StartTargets;

        YDebug() << "Exporting JSON" << Endl;

        FORCE_TRACE(U, NEvent::TStageStarted("Visit JSON"));
        THolder<TJSONVisitor> cmdbuilderHolder;
        THolder<TUidsData> uidsCache;
        if (!conf.ShouldLoadUidsCacheEarly()) {
            yMake.LoadUidsAsync(exec);
        }
        if (yMake.UidsCacheLoadingCompletedPtr) {
            uidsCache = co_await yMake.UidsCacheLoadingCompletedPtr->async_receive();
        }
        auto cmdBuilderReset = [&]() {
            if (uidsCache) {
                cmdbuilderHolder.Reset(new TJSONVisitor(std::move(*uidsCache), yMake.GetRestoreContext(), yMake.Commands, graph.Names().CommandConf, startTargets));
            } else {
                cmdbuilderHolder.Reset(new TJSONVisitor(yMake.GetRestoreContext(), yMake.Commands, graph.Names().CommandConf, startTargets));
            }
        };

        cmdBuilderReset();

        TJSONVisitor& cmdbuilder = *cmdbuilderHolder.Get();

        IterateAll(graph, startTargets, cmdbuilder, [](const TTarget& t) -> bool {
            return !t.IsNonDirTarget && !t.IsModuleTarget;
        });
        FORCE_TRACE(U, NEvent::TStageFinished("Visit JSON"));

        {
            NYMake::TTraceStageWithTimer renderJsonTimer("Render JSON", MON_NAME(EYmakeStats::RenderJSONTime));

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

                if (!conf.ShouldLoadJsonCacheEarly()) {
                    yMake.LoadJsonCacheAsync(exec);
                }
                auto cachePtr = co_await yMake.JSONCacheLoadingCompletedPtr->async_receive();
                if (!cachePtr) {
                    cachePtr = MakeHolder<TMakePlanCache>(yMake.Conf);
                }
                auto& cache = *cachePtr.Get();
                plan.WriteConf();

                if (yMake.Conf.ParallelRendering) {
                    // Uids must be updated in depth-first order
                    for (const auto& nodeId: cmdbuilder.GetOrderedNodes()) {
                        UpdateUids(yMake.Graph, cmdbuilder, nodeId);
                    }
                    TMakeModuleParallelStates modulesStatesCache{yMake.Conf, yMake.Graph, yMake.Modules};
                    auto strand = asio::make_strand(exec);
                    using TChannel = asio::experimental::concurrent_channel<void(asio::error_code)>;
                    TDeque<TChannel> writeChannels;
                    TDeque<TString> jsonStrings;
                    for (const auto& [_, nodes]: cmdbuilder.GetTopoGenerations()) {
                        size_t chunkSize = 10;
                        // group nodes by module preserving order
                        std::map<TNodeId, TVector<TNodeId>> nodesByModuleId;
                        for (const auto& nodeId : nodes) {
                            nodesByModuleId[cmdbuilder.GetModuleByNode(nodeId)].push_back(nodeId);
                        }

                        TDeque<TChannel> renderChannels;
                        auto moduleIt = nodesByModuleId.begin();
                        while (moduleIt != nodesByModuleId.end()) {
                            auto chunkEnd = std::next(moduleIt, std::min(chunkSize, size_t(std::distance(moduleIt, nodesByModuleId.end()))));
                            const auto chunk = std::ranges::subrange(moduleIt, chunkEnd);

                            renderChannels.push_back({exec, 1u});
                            auto& renderChannel = renderChannels.back();
                            writeChannels.push_back({exec, 1u});
                            auto& writeChannel = writeChannels.back();
                            jsonStrings.push_back(TString{});
                            auto& jsonString = jsonStrings.back();
                            asio::co_spawn(exec, [&cmdbuilder, &cache, &modulesStatesCache, &graph, &yMake, chunk, &renderChannel, &plan, strand, &writeChannel, &jsonString]() -> asio::awaitable<void> {
                                TStringOutput ss(jsonString);
                                NYMake::TJsonWriter writer{ss};
                                NYMake::TJsonWriter::TOpenedArray nodesArr;
                                for (const auto& [modId, nodeIds] : chunk) {
                                    for (const auto& nodeId : nodeIds) {
                                        const auto& node = cmdbuilder.Nodes.at(nodeId);
                                        RenderOrRestoreJSONNode(yMake, cmdbuilder, nodesArr, cache, nodeId, node, writer, modulesStatesCache);
                                        if (IsModuleType(graph[nodeId]->NodeType)) {
                                            // remove state from cache to free the memory
                                            modulesStatesCache.ClearState(nodeId);
                                            TProgressManager::Instance()->IncRenderModulesDone();
                                        }
                                    }
                                }
                                co_await renderChannel.async_send(std::error_code{});
                                writer.Flush();
                                asio::co_spawn(strand, [&plan, &jsonString, &writeChannel]() -> asio::awaitable<void> {
                                    plan.Writer.WriteArrayJsonValue(plan.NodesArr, std::move(jsonString));
                                    co_await writeChannel.async_send(std::error_code{});
                                }, asio::detached);
                            }, [&renderChannel, &writeChannel](std::exception_ptr ptr) {
                                if (ptr) {
                                    renderChannel.cancel();
                                    writeChannel.cancel();
                                    std::rethrow_exception(ptr);
                                }
                            });
                            moduleIt = chunkEnd;
                        }
                        for (auto& renderChannel : renderChannels) {
                            co_await renderChannel.async_receive();
                        }
                    }
                    for (auto& writeChannel : writeChannels) {
                        co_await writeChannel.async_receive();
                    }
                } else {
                    TMakeModuleSequentialStates modulesStatesCache{yMake.Conf, yMake.Graph, yMake.Modules};
                    for (const auto& nodeId: cmdbuilder.GetOrderedNodes()) {
                        UpdateUids(yMake.Graph, cmdbuilder, nodeId);

                        const auto& node = cmdbuilder.Nodes.at(nodeId);
                        RenderOrRestoreJSONNode(yMake, cmdbuilder, plan.NodesArr, cache, nodeId, node, plan.Writer, modulesStatesCache);
                        if (IsModuleType(graph[nodeId]->NodeType)) {
                            TProgressManager::Instance()->IncRenderModulesDone();
                        }
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
                if (resultIt->second.OutTogetherDependency != TNodeId::Invalid && !resultIt->second.HasBuildCmd) {
                    resultIt = cmdbuilder.Nodes.find(resultIt->second.OutTogetherDependency);
                }
                plan.Results.push_back(resultIt->second.GetNodeUid());
            }
            std::sort(plan.Results.begin(), plan.Results.end());
        }
        yMake.SaveUids(&cmdbuilder);
        cmdbuilder.ReportCacheStats();
        if (cmdbuilder.ErrorShower.Count != 0) {
            YErr() << "Expression errors found: " << cmdbuilder.ErrorShower.Count << Endl;
        }
    }

    class TOutputStreamWrapper {
    public:
        TOutputStreamWrapper(const TString& outFile, const TString& compressionCodec, IOutputStream* defaultOutStream) {
            if (outFile == "-") {
                OutputStreamPtr_ = defaultOutStream;
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

asio::awaitable<void> ExportJSON(TYMake& yMake, asio::any_io_executor exec) {
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

        TOutputStreamWrapper output{conf.WriteJSON, conf.JsonCompressionCodec, yMake.Conf.OutputStream.Get()};
        NYMake::TJsonWriter jsonWriter(*output.Get());
        TMakePlan plan(jsonWriter);
        co_await RenderJSONGraph(yMake, plan, exec);
    }

    conf.EnableRealPathCache(nullptr);
    conf.SourceRoot = sysSourceRoot;
    conf.BuildRoot = sysBuildRoot;
    conf.NormalizeRealPath = sysNormalize;

    FORCE_TRACE(U, NEvent::TStageFinished("Export JSON"));

    yMake.JSONCacheMonEvent();
    yMake.UidsCacheMonEvent();
}
