#include "graph.h"

#include <devtools/ya/cpp/lib/edl/json/from_json.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/hash_set.h>

#include <ranges>
#include <span>

namespace NYa::NGraph {
    size_t TGraph::Size() const {
        return Graph.size();
    }

    void TGraph::AddGlobalResources(const TVector<TGlobalResource>& resources) {
        THashSet<TGraphString> Patterns;
        for (const TGlobalResource& res : Conf.Resources) {
            Patterns.insert(res.Pattern);
        }
        for (const TGlobalResource& res : resources) {
            if (!Patterns.contains(res.Pattern)) {
                Conf.Resources.push_back(res);
                Patterns.insert(res.Pattern);
            }
        }
    }

    void TGraph::SetTags(const TVector<TGraphString>& tags) {
        for (TNodePtr node : Graph) {
            node->Tags = tags;
        }
    }

    void TGraph::SetPlatform(const TGraphString platform) {
        for (TNodePtr node : Graph) {
            node->Platform = platform;
        }
    }

    void TGraph::AddHostMark(bool sandboxing) {
        for (TNodePtr node : Graph) {
            node->HostPlatform = true;
            node->Sandboxing = sandboxing;
        }
    }

    // Add missing tool deps
    void TGraph::AddToolDeps() {
        for (TNodePtr node : Graph) {
            if (node->ForeignDeps && node->ForeignDeps->Tool) {
                TVector<TUid>& deps = node->Deps;
                THashSet<TUid> existingDeps{deps.begin(), deps.end()};
                std::copy_if(
                    node->ForeignDeps->Tool.begin(),
                    node->ForeignDeps->Tool.end(),
                    std::back_inserter(deps),
                    [&](TUid uid) {return !existingDeps.contains(uid);}
                );
            }
        }
    }

    void TGraph::AddSandboxingMark() {
        for (TNodePtr node : Graph) {
            if (!node->HostPlatform) {
                node->Sandboxing = true;
            }
        }
    }

    void TGraph::UpdateConf(const TConf& conf) {
        Y_ENSURE(!conf.Resources, "UpdateConf() should not be used for resources update");
        for (const auto& [k, v] : conf.Remainder) {
            Conf.Remainder[k] = v;
        }
    }

    static void AddNode(TNodeList& nodeList, TUid uid, THashMap<TUid, TNodePtr>& nodesMap) {
        // nodesMap has 2 purposes: getting a node by an uid or mark an uid as already visited
        if (TNodePtr node = nodesMap.at(uid)) {
            nodeList.push_back(node);
            nodesMap[uid] = {}; // mark node as visited (zero pointer)
            for (TUid uid : node->Deps) {
                AddNode(nodeList, uid, nodesMap);
            }
        }
    }

    void TGraph::Strip() {
        // Remove duplicate resources
        THashSet<TGraphString> Patterns;
        auto destPos = Conf.Resources.begin();
        for (auto curPos = Conf.Resources.begin(); curPos != Conf.Resources.end(); ++curPos) {
            if (auto&& [_, inserted] = Patterns.insert(curPos->Pattern); inserted) {
                if (destPos != curPos) {
                    *destPos = std::move(*curPos);
                }
                ++destPos;
            }
        }
        Conf.Resources.resize(destPos - Conf.Resources.begin());

        // Remove unreachable nodes
        THashMap<TUid, TNodePtr> nodesMap{};
        for (TNodePtr node : Graph) {
            nodesMap.try_emplace(node->Uid, node);
        }
        Graph.clear();
        for (TUid uid : Result) {
            AddNode(Graph, uid, nodesMap);
        }
    }

    template <class T, class... Args>
    static void InsertTo(T& dest, const Args&... source) {
        (dest.insert(source.begin(), source.end()), ...);
    }

    template <class T, class... Args>
    static void InsertTo(TVector<T>& dest, const Args&... source) {
        (dest.insert(dest.end(), source.begin(), source.end()), ...);
    }

    using TModuleKey = std::pair<TGraphString, TGraphString>;
    using TModuleMapping = THashMap<TModuleKey, TNodePtr>;
    using TUidMapping = THashMap<TUid, TUid>;

    TModuleKey GetModuleKey(TNodePtr node) {
        return {node->TargetProperties.ModuleDir, node->TargetProperties.ModuleTag};
    }

    static inline bool IsCachable(TNodePtr node) {
        if (!node->Cache.GetOrElse(true)) {
            return false;
        }
        if (auto it = node->Kv.find(TGraphString{"disable_cache"}); it != node->Kv.end() && it->second.GetBoolean()) {
            return false;
        }
        return true;
    }

    static void UpdateCacheAttributes(const TNodePtr src, TNodePtr dest) {
        if (!IsCachable(src)) [[unlikely]] {
            dest->Cache = false;
        }
    }

    static TNodePtr GenMoveNode(TNodePtr old, const TGraphString from, const TGraphString& to) {
        TNodePtr mvNode = CreateNode();
        mvNode->Uid = MD5::Calc(TString(old->Uid) + "-" + to);
        mvNode->Deps = {old->Uid};
        mvNode->Inputs = {from};
        mvNode->Outputs = {to};
        mvNode->Priority = 0;
        mvNode->Cmds = {TCmd{.CmdArgs={{TGraphString{"/bin/mv"}, from , to}}}};
        mvNode->Kv = {
            std::make_pair(TGraphString{"p"}, NJson::TJsonValue{"CP"}),
            std::make_pair(TGraphString{"pc"}, NJson::TJsonValue{"light-blue"}),
        };
        mvNode->Cwd = "$(BUILD_ROOT)";
        UpdateCacheAttributes(old, mvNode);
        mvNode->Type = 2;
        if (FindPtr(old->TaredOutputs, from)) {
            mvNode->TaredOutputs.push_back(to);
        }
        mvNode->Tags = old->Tags;
        mvNode->HostPlatform = old->HostPlatform;

        return mvNode;
    }

    struct TToolNodeDiscoveringResult {
        TUidMapping ToolMapping;
        TVector<TString> UnmatchedTargetToolDirs;
    };

    // Helper to get TNodePtr instead of TNodePtr* from map.
    template <class M, class K>
    inline TNodePtr FindNode(const M& map, const K& key) {
        const TNodePtr* ptr = map.FindPtr(key);
        return ptr ? *ptr : TNodePtr{};
    }

    static TToolNodeDiscoveringResult GetToolMapping(
        TNodeList& extraNodes,
        TModuleMapping& toolModules,
        const std::ranges::input_range auto& targetNodes,
        const THashSet<TUid>& targetToolUids
    ) {
        TUidMapping toolMapping{};
        THashSet<TModuleKey> unmatchedTargetToolModules{};
        TModuleMapping mvNodes{};

        for (const TNodePtr targetNode: targetNodes) {
            const TUid& uid = targetNode->Uid;
            if (targetToolUids.contains(uid)) {
                TModuleKey moduleKey = GetModuleKey(targetNode);
                if (TNodePtr toolNode = FindNode(toolModules, moduleKey)) {
                    UpdateCacheAttributes(targetNode, toolNode);
                    TGraphString targetMainOutput = targetNode->Outputs[0];
                    TGraphString toolMainOutput = toolNode->Outputs[0];
                    if (targetMainOutput == toolMainOutput) {
                        toolMapping.emplace(uid, toolNode->Uid);
                    } else {
                        // host and target platforms may disagree about output naming
                        TNodePtr mvNode = FindNode(mvNodes, moduleKey);
                        if (!mvNode) {
                            mvNode = GenMoveNode(toolNode, toolMainOutput, targetMainOutput);
                            mvNodes.emplace(moduleKey, mvNode);
                            extraNodes.push_back(mvNode);
                        } else {
                            UpdateCacheAttributes(targetNode, mvNode);
                        }
                        toolMapping[uid] = mvNode->Uid;
                    }
                } else {
                    unmatchedTargetToolModules.insert(moduleKey);
                }
            }
        }

        TVector<TString> unmatchedTargetToolDirs{};
        for (const auto& moduleKey : unmatchedTargetToolModules) {
            unmatchedTargetToolDirs.push_back(TString(moduleKey.first));
        }
        return {.ToolMapping = std::move(toolMapping), .UnmatchedTargetToolDirs = std::move(unmatchedTargetToolDirs)};
    }

    // Searching for tools in the target nodes and prepare them for replacing by the host ones
    static TToolNodeDiscoveringResult DiscoverAndPrepareToolNodes(
        TNodeList& extraNodes,
        const TNodeList& toolNodes,
        const std::ranges::forward_range auto& targetNodes
    ) {
        TModuleMapping toolModules{};
        for (TNodePtr toolNode : toolNodes) {
            if (toolNode->TargetProperties.ModuleType != EModuleType::Undefined) {
                toolModules.emplace(GetModuleKey(toolNode), toolNode);
            }
        }

        THashSet<TUid> targetToolUids{};
        for (TNodePtr targetNode : targetNodes) {
            if (targetNode->ForeignDeps && targetNode->ForeignDeps->Tool) {
                targetToolUids.insert(targetNode->ForeignDeps->Tool.begin(), targetNode->ForeignDeps->Tool.end());
            }
        }

        return GetToolMapping(extraNodes, toolModules, targetNodes, targetToolUids);
    }

    static void ApplyUidMapping(std::ranges::forward_range auto& targetNodes, const TUidMapping& toolMapping, const TUidMapping& targetMapping = {}) {
        for (TNodePtr targetNode : targetNodes) {
            TVector<TUid>& deps = targetNode->Deps;
            bool isToolPresent = targetNode->ForeignDeps && targetNode->ForeignDeps->Tool;
            // TODO YMAKE-832: Totally remove this 'if' when tool uids are removed from target deps
            if (isToolPresent) {
                THashSet<TUid> toolUids{targetNode->ForeignDeps->Tool.begin(), targetNode->ForeignDeps->Tool.end()};
                auto destPos = deps.begin();
                for (auto curPos = deps.begin(); curPos != deps.end(); ++curPos) {
                    if (!toolUids.contains(*curPos)) {
                        if (curPos != destPos) {
                            *destPos = *curPos;
                        }
                        ++destPos;
                    }
                }
                deps.resize(destPos - deps.begin());
            }
            // End of TODO
            if (targetMapping) {
                for (auto& dep : deps) {
                    if (const TUid* newDepPtr = targetMapping.FindPtr(dep)) {
                        dep = *newDepPtr;
                    }
                }
            }
            if (isToolPresent) {
                for (const TUid& uid : targetNode->ForeignDeps->Tool) {
                    TUid newUid = uid;
                    if (const TUid* u = toolMapping.FindPtr(uid)) {
                        newUid = *u;
                    }
                    deps.push_back(newUid);
                }
            }
        }
    }

    TGraphMergingResult MergeSingleGraph(TGraphPtr tools, TGraphPtr target) {
        TNodeList extraNodes{};
        TToolNodeDiscoveringResult discoveringResult = DiscoverAndPrepareToolNodes(extraNodes, tools->Graph, target->Graph);

        ApplyUidMapping(target->Graph, discoveringResult.ToolMapping);

        TGraphPtr merged = CreateGraph();
        InsertTo(merged->Conf.Resources, tools->Conf.Resources, target->Conf.Resources);
        InsertTo(merged->Graph, extraNodes, target->Graph, tools->Graph);
        InsertTo(merged->Inputs, target->Inputs, tools->Inputs);
        merged->Result = target->Result;

        return {.Merged = merged, .UnmatchedTargetToolDirs = std::move(discoveringResult.UnmatchedTargetToolDirs)};
    }

    TGraphMergingResult MergeGraphs(TGraphPtr tools, TGraphPtr pic, TGraphPtr noPic) {
        if (!pic || pic == noPic) {
            return MergeSingleGraph(tools, noPic);
        } else if (!noPic) {
            return MergeSingleGraph(tools, pic);
        }

        TNodeList extraNodes{};
        auto targetNodes = std::ranges::join_view(std::array{std::span{pic->Graph}, std::span{noPic->Graph}});

        TToolNodeDiscoveringResult discoveringResult = DiscoverAndPrepareToolNodes(extraNodes, tools->Graph, targetNodes);

        THashSet<TUid> noPicResults;
        InsertTo(noPicResults, noPic->Result);
        TNodeList noPicResultNodes;
        TUidMapping targetMapping;

        TModuleMapping picLibraries;
        TModuleMapping picBinaries;
        for (TNodePtr picNode : pic->Graph) {
            if (picNode->IsDynLibrary()) {
                picLibraries.emplace(GetModuleKey(picNode), picNode);
            } else if (picNode->IsBinary()) {
                picBinaries.emplace(GetModuleKey(picNode), picNode);
            }
        }

        for (TNodePtr noPicNode : noPic->Graph) {
            if (noPicResults.contains(noPicNode->Uid)) {
                noPicResultNodes.push_back(noPicNode);
            }
            if (noPicNode->IsDynLibrary()) {
                if (TNodePtr picNode = FindNode(picLibraries, GetModuleKey(noPicNode))) [[likely]] {
                    // Schedule replacing of no_pic dyn library by a pic version
                    targetMapping.emplace(noPicNode->Uid, picNode->Uid);
                    UpdateCacheAttributes(noPicNode, picNode);
                }
            } else if (noPicNode->IsBinary()) {
                if (TNodePtr picNode = FindNode(picBinaries, GetModuleKey(noPicNode))) [[likely]] {
                    targetMapping.emplace(picNode->Uid, noPicNode->Uid);
                    // Schedule replacing of pic binary by a no_pic version
                    UpdateCacheAttributes(picNode, noPicNode);
                }
            }
        }

        // Apply scheduled deps replacing
        ApplyUidMapping(targetNodes, discoveringResult.ToolMapping, targetMapping);

        THashSet<TUid> newResult;

        for (TNodePtr noPicNode : noPicResultNodes) {
            if (!noPicNode->IsDynLibrary()) {
                newResult.insert(noPicNode->Uid);
            } else if (const auto uid = targetMapping.FindPtr(noPicNode->Uid)) {
                newResult.insert(*uid);
            }
        }

        TGraphPtr merged = CreateGraph();
        InsertTo(merged->Conf.Resources, tools->Conf.Resources, pic->Conf.Resources, noPic->Conf.Resources);
        InsertTo(merged->Graph, noPic->Graph, extraNodes, pic->Graph, tools->Graph);
        InsertTo(merged->Inputs, pic->Inputs, noPic->Inputs, tools->Inputs);
        InsertTo(merged->Result, newResult);
        Sort(merged->Result);

        return {.Merged = merged, .UnmatchedTargetToolDirs = std::move(discoveringResult.UnmatchedTargetToolDirs)};
    }

    void CleanStorage() {
        TInternStringStorage::Instance().Clear();
    }
}
