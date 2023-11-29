#include "read_sem_graph.h"

#include <devtools/ymake/compact_graph/query.h>

#include <library/cpp/json/json_reader.h>

#include <spdlog/spdlog.h>

#include <span>

namespace {

    struct TDepInfo {
        ui32 From;
        ui32 To;
        EDepType Type;
    };

    class TSemGraphCallbacks: public NJson::TJsonCallbacks {
    private:
        enum class EPathElem {
            Obj,
            Arr,
            Data,
            Semantics,
            Sem,
            DataType,
            Id,
            Name,
            NodeType,
            Tag,
            FromId,
            ToId,
            DepType,
            ManagedPeersClosure,
            Tools,
            Tests,
            SkippedKey,
        };

        enum class ECurElem {
            Node,
            Dep,
            Unspecified
        };

        struct TCurElemData {
            ECurElem DataType = ECurElem::Unspecified;
            TSemNodeData Node;
            TMaybe<ui32> Id;
            TMaybe<ui32> FromId;
            TMaybe<ui32> ToId;
            TMaybe<EDepType> DepType;
            TVector<ui32> ManagedPeersClosure;
            TVector<ui32> Tools;
            TVector<ui32> Tests;
            bool StartDir = false;
        };

    public:
        TSemGraphCallbacks(TSemGraph& graph, bool useManagedPeersClosure) noexcept
            : Graph{graph}
            , UseManagedPeersClosure_(useManagedPeersClosure)
        {}

        bool OnOpenMap() override {
            Open(EPathElem::Obj);
            if (IsSemanticsPath()) {
                CurElem.Node.Sem.push_back({});
            }
            return true;
        }
        bool OnCloseMap() override {
            if (IsElemPath()) {
                const auto curElem = std::exchange(CurElem, {});
                switch (curElem.DataType) {
                    case ECurElem::Unspecified:
                        return false;
                    case ECurElem::Dep: {
                        if (!curElem.FromId || !curElem.ToId || !curElem.DepType) {
                            return false;
                        }
                        TDepInfo dep{
                            .From = curElem.FromId.GetRef(),
                            .To = curElem.ToId.GetRef(),
                            .Type = curElem.DepType.GetRef()
                        };
                        const auto nodeFromId = Id2Node[dep.From];
                        const auto& nodeFrom = Graph.Get(nodeFromId);
                        const auto& nodeTo = Graph.Get(Id2Node[dep.To]);
                        if (UseManagedPeersClosure_ // if enabled managed peers closure instead direct peers
                            && ManagedPeersClosureToLink.find(nodeFromId) != ManagedPeersClosureToLink.end() // and nodeFrom has managed peers closure
                            && IsDirectPeerdirDep(nodeFrom->NodeType, dep.Type, nodeTo->NodeType)) { // then skip direct peersfrom sem graph
                            break;
                        }
                        if (!Link(dep))
                            PostponedDeps.push_back(dep);
                        break;
                    }
                    case ECurElem::Node:
                        if (!curElem.Id) {
                            return false;
                        }
                        const auto nodeId = Graph.AddNode(curElem.Node).Id();
                        if (!Id2Node.insert({curElem.Id.GetRef(), nodeId}).second) {
                            return false;
                        }
                        if (curElem.StartDir) {
                            StartDirs.push_back(nodeId);
                        }
                        if (!curElem.ManagedPeersClosure.empty()) {
                            ManagedPeersClosureToLink.emplace(nodeId, std::move(curElem.ManagedPeersClosure));
                        }
                        if (!curElem.Tools.empty()) {
                            ToolsToLink.emplace(nodeId, std::move(curElem.Tools));
                        }
                        if (!curElem.Tests.empty()) {
                            TestsToLink.emplace(nodeId, std::move(curElem.Tests));
                        }
                        break;
                }
            }
            if (Path.size() == 1) {
                for (const auto& dep: PostponedDeps) {
                    if (!Link(dep)) {
                        throw TReadGraphException() << fmt::format(
                            "Failed to add dependency {} -[{}]-> {}",
                            dep.From,
                            ToString(dep.Type),
                            dep.To
                        );
                    }
                }
            }
            Close(EPathElem::Obj);
            return true;
        }

        bool OnOpenArray() override {
            Open(EPathElem::Arr);
            return true;
        }
        bool OnCloseArray() override {
            Close(EPathElem::Arr);
            return true;
        }

        bool OnMapKey(const TStringBuf& key) override {
            if (key == "data") {
                LastKey = EPathElem::Data;
            } else if (key == "semantics") {
                LastKey = EPathElem::Semantics;
            } else if (key == "sem") {
                LastKey = EPathElem::Sem;
            } else if (key == "DataType") {
                LastKey = EPathElem::DataType;
            } else if (key == "Id") {
                LastKey = EPathElem::Id;
            } else if (key == "Name") {
                LastKey = EPathElem::Name;
            } else if (key == "NodeType") {
                LastKey = EPathElem::NodeType;
            } else if (key == "Tag") {
                LastKey = EPathElem::Tag;
            } else if (key == "FromId") {
                LastKey = EPathElem::FromId;
            } else if (key == "ToId") {
                LastKey = EPathElem::ToId;
            } else if (key == "ManagedPeersClosure") {
                LastKey = EPathElem::ManagedPeersClosure;
            } else if (key == "Tools") {
                LastKey = EPathElem::Tools;
            } else if (key == "Tests") {
                LastKey = EPathElem::Tests;
            } else if (key == "DepType") {
                LastKey = EPathElem::DepType;
            } else {
                LastKey = EPathElem::SkippedKey;
            }

            return true;
        }

        bool OnString(const TStringBuf& val) override {
            if (IsSemItemPath() && !LastKey) {
                Y_ASSERT(!CurElem.Node.Sem.empty());
                CurElem.Node.Sem.back().push_back(TString{val}); // TODO: stringpool???
                return true;
            }
            if (!LastKey || LastKey == EPathElem::SkippedKey) {
                return true;
            }

            if (IsElemPath()) {
                if (LastKey == EPathElem::DataType) {
                    if (val == "Node") {
                        CurElem.DataType = ECurElem::Node;
                    } else if (val == "Dep") {
                        CurElem.DataType = ECurElem::Dep;
                    } else {
                        return false;
                    }
                } else if (LastKey == EPathElem::NodeType) {
                    CurElem.Node.NodeType = FromString<EMakeNodeType>(val);
                } else if (LastKey == EPathElem::DepType) {
                    CurElem.DepType = FromString<EDepType>(val);
                } else if (LastKey == EPathElem::Name) {
                    CurElem.Node.Path = val;
                } else if (LastKey == EPathElem::Tag) {
                    if (val == "StartDir") {
                        CurElem.StartDir = true;
                    }
                }
            }
            return true;
        }
        bool OnInteger(long long val) override {
            if (IsManagedPeersClosurePath()) {
                CurElem.ManagedPeersClosure.push_back(static_cast<ui32>(val));
                return true;
            }
            if (IsToolsPath()) {
                CurElem.Tools.push_back(static_cast<ui32>(val));
                return true;
            }
            if (IsTestsPath()) {
                CurElem.Tests.push_back(static_cast<ui32>(val));
                return true;
            }

            if (!LastKey || LastKey == EPathElem::SkippedKey) {
                return true;
            }

            if (IsElemPath()) {
                if (LastKey == EPathElem::Id) {
                    CurElem.Id = static_cast<ui32>(val);
                } else if (LastKey == EPathElem::FromId) {
                    CurElem.FromId = static_cast<ui32>(val);
                } else if (LastKey == EPathElem::ToId) {
                    CurElem.ToId = static_cast<ui32>(val);
                }
            }

            return true;
        }

        TVector<TNodeId> TakeStartDirs() noexcept {
            return std::move(StartDirs);
        }

    private:
        bool Link(TDepInfo dep) {
            const auto from = Id2Node.find(dep.From);
            const auto to = Id2Node.find(dep.To);
            if (from == Id2Node.end() || to == Id2Node.end()) {
                return false;
            }
            Graph.AddEdge(from->second, to->second, dep.Type);
            return true;
        }

        bool LinkManagedPeersClosure(TNodeId outputNodeId, std::span<const ui32> managedPeersClosureElemIds) {
            bool hasErrors = false;
            for (ui32 managedPeersClosureElemId: managedPeersClosureElemIds) {
                const auto it = Id2Node.find(managedPeersClosureElemId);
                if (it == Id2Node.end()) {
                    spdlog::error("node {} references unknown managed closure peer {}", Graph[outputNodeId]->Path, managedPeersClosureElemId);
                    hasErrors = true;
                    continue; // continue processing graph without edge to unknown node
                }
                Graph.AddEdge(outputNodeId, it->second, EDT_BuildFrom);
            }
            return hasErrors;
        }

        bool LinkTools(TNodeId outputNodeId, std::span<const ui32> toolElems) {
            bool hasErrors = false;
            const auto toolCmd = Graph.AddNode({.Path = TString{TOOL_NODES_FAKE_PATH}, .NodeType = EMNT_BuildCommand, .Sem = {}});
            Graph.AddEdge(outputNodeId, toolCmd.Id(), EDT_BuildCommand);
            for (ui32 tool: toolElems) {
                const auto it = Id2Node.find(tool);
                if (it == Id2Node.end()) {
                    spdlog::error("node {} references unknown tool {}", Graph[outputNodeId]->Path, tool);
                    hasErrors = true;
                    continue; // continue processing graph without edge to unknown node
                }
                Graph.AddEdge(toolCmd.Id(), it->second, EDT_Include);
            }
            return hasErrors;
        }

        bool LinkTests(TNodeId mod, std::span<const ui32> testElems) {
            bool hasErrors = false;
            const auto prop = Graph.AddNode({.Path = TString{TESTS_PROPERTY_FAKE_PATH}, .NodeType = EMNT_Property, .Sem = {}});
            Graph.AddEdge(mod, prop.Id(), EDT_Property);
            for (ui32 test: testElems) {
                const auto it = Id2Node.find(test);
                if (it == Id2Node.end()) {
                    spdlog::error("module {} references unknown test {}", Graph[mod]->Path, test);
                    hasErrors = true;
                    continue; // continue processing graph without edge to unknown node
                }
                Graph.AddEdge(prop.Id(), it->second, EDT_Include);
            }
            return hasErrors;
        }

        void Open(EPathElem kind) {
            if (const auto last = std::exchange(LastKey, {})) {
                Path.push_back(last.GetRef());
            }
            Path.push_back(kind);
        }

        void Close(EPathElem kind [[maybe_unused]]) {
            Y_ASSERT(!Path.empty());
            Y_ASSERT(Path.back() == kind);
            Path.pop_back();
            if (!Path.empty() && Path.back() != EPathElem::Arr) {
                Y_ASSERT(Path.back() != EPathElem::Obj);
                Path.pop_back();
            }
            LastKey = {};

            if (Path.empty()) {
                bool hasErrors = false;
                if (UseManagedPeersClosure_) {
                    for (const auto& [nodeId, peerElems]: ManagedPeersClosureToLink)
                        hasErrors |= LinkManagedPeersClosure(nodeId, peerElems);
                }
                for (const auto& [nodeId, toolElems]: ToolsToLink)
                    hasErrors |= LinkTools(nodeId, toolElems);
                for (const auto& [nodeId, testElems]: TestsToLink)
                    hasErrors |= LinkTests(nodeId, testElems);
                if (hasErrors)
                    throw yexception() << "Inconsistent semantic graph JSON file";
            }
        }

        bool IsElemPath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(GraphElemPrefix), std::end(GraphElemPrefix));
        }

        bool IsSemanticsPath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(SemanticsItem), std::end(SemanticsItem));
        }

        bool IsSemItemPath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(SemList), std::end(SemList));
        }

        bool IsManagedPeersClosurePath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(ManagedPeersClosureArr), std::end(ManagedPeersClosureArr));
        }

        bool IsToolsPath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(ToolsArr), std::end(ToolsArr));
        }

        bool IsTestsPath() const noexcept {
            return std::equal(Path.begin(), Path.end(), std::begin(TestsArr), std::end(TestsArr));
        }

    private:
        constexpr static EPathElem GraphElemPrefix[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj};
        constexpr static EPathElem ManagedPeersClosureArr[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj, EPathElem::ManagedPeersClosure, EPathElem::Arr};
        constexpr static EPathElem ToolsArr[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj, EPathElem::Tools, EPathElem::Arr};
        constexpr static EPathElem TestsArr[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj, EPathElem::Tests, EPathElem::Arr};
        constexpr static EPathElem SemanticsItem[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj, EPathElem::Semantics, EPathElem::Arr, EPathElem::Obj};
        constexpr static EPathElem SemList[] = {EPathElem::Obj, EPathElem::Data, EPathElem::Arr, EPathElem::Obj, EPathElem::Semantics, EPathElem::Arr, EPathElem::Obj, EPathElem::Sem, EPathElem::Arr};

        // Cur elem
        TCurElemData CurElem;

        // Document pos
        TMaybe<EPathElem> LastKey;
        TVector<EPathElem> Path;

        // Global mappings
        THashMap<ui32, TNodeId> Id2Node;
        THashMap<TNodeId, TVector<ui32>> ManagedPeersClosureToLink;
        THashMap<TNodeId, TVector<ui32>> ToolsToLink;
        THashMap<TNodeId, TVector<ui32>> TestsToLink;
        TDeque<TDepInfo> PostponedDeps;

        TSemGraph& Graph;
        TVector<TNodeId> StartDirs;
        bool UseManagedPeersClosure_;
    };
}

std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(const std::filesystem::path& path, bool useManagedPeersClosure) {
    TFileInput in{path.string()};
    return ReadSemGraph(in, useManagedPeersClosure);
}

std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(IInputStream& in, bool useManagedPeersClosure) {
    auto res = MakeHolder<TSemGraph>();
    TSemGraphCallbacks semGraphCallbacks{*res, useManagedPeersClosure};
    NJson::ReadJson(&in, &semGraphCallbacks);
    return std::make_pair(std::move(res), semGraphCallbacks.TakeStartDirs());
}
