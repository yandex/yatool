#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/makefile_loader.h>
#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_restorer.h>
#include <devtools/ymake/module_resolver.h>
#include <devtools/ymake/ymake.h>
#include <devtools/ymake/conf.h>

#include <util/generic/yexception.h>
#include <util/folder/pathsplit.h>
#include <util/folder/dirut.h>
#include <util/string/split.h>
#include <util/generic/list.h>
#include <util/generic/stack.h>

// REMOVEME
#include <util/folder/iterator.h>
#include <util/system/fs.h>
// END

struct TIFixEntryStatsData : public TEntryStatsData {
    bool MayReEnter = false;
};

using TIFixEntryStats = TVisitorStateItem<TIFixEntryStatsData>;

struct TIFixStData : public TGraphIteratorStateItemBase<false> {
    using TGraphIteratorStateItemBase::TGraphIteratorStateItemBase;
    THashMap<TNodeId, TVector<TNodeId>> LocalRepl;
    bool Fixed = false;
};

struct TIncFixer: public TDirectPeerdirsVisitor<TIFixEntryStats, TIFixStData> {
    using TBase = TDirectPeerdirsVisitor<TIFixEntryStats, TIFixStData>;
    using TState = typename TBase::TState;

    const TBuildConfiguration& Conf;
    TUpdIter& UpdIter;
    TDepGraph& Graph;
    TModules& Modules;
    TIncParserManager& IncParserManager;
    TString NewName; // temporary strings
    TStack<bool> DontResolveIncludesForModules;
    bool UnlinkLastNode = false;
    int ResolvedToNothing = 0;
    int ResolvedToExisting = 0;
    int ResolvedToNew = 0;

    TIncFixer(const TBuildConfiguration& conf, TDepGraph& graph, TUpdIter& updIter, TModules& modules,
              TIncParserManager& incParserManager)
        : Conf(conf)
        , UpdIter(updIter)
        , Graph(graph)
        , Modules(modules)
        , IncParserManager(incParserManager) {
        DontResolveIncludesForModules.push(false);
    }

    ~TIncFixer() {
        Y_ASSERT(DontResolveIncludesForModules.size() == 1u);
    }

    bool Enter(TState& state) {
        const bool fresh = TBase::Enter(state);
        const TDepTreeNode& node = state.TopNode().Value();

        if (IsModuleType(node.NodeType)) {
            TModuleRestorer restorer({Conf, Graph, Modules}, state.TopNode());
            TModule& module = *restorer.RestoreModule();
            DontResolveIncludesForModules.push(module.GetAttrs().DontResolveIncludes);
            return fresh;
        }

        const bool enter = fresh || (CurEnt->MayReEnter && !DontResolveIncludesForModules.top());
        if (enter) {
            CurEnt->MayReEnter = DontResolveIncludesForModules.top();
        }

        if (DontResolveIncludesForModules.top()) {
            return enter;
        }

        if (Y_LIKELY(node.NodeType != EMNT_MissingFile || !state.HasIncomingDep()))
            return enter;

        const TDepTreeNode& parentNode = state.Parent()->Node().Value();
        auto isIncludeDep = *state.IncomingDep() == EDT_Include;
        auto isBuildFromDep = *state.IncomingDep() == EDT_BuildFrom;
        // HINT: no include parsing an processing is performed for NonParsedFile -[BuildFrom]-> *Command -[Property]-> File
        // is performed so *File -[BuildFrom]-> *Command is skipped in the  traverse
        if (!(isIncludeDep || isBuildFromDep) || !IsFileType(parentNode.NodeType) || !IsFileType(node.NodeType)) {
            return enter;
        }

        TFileView parentName = Graph.Names().FileConf.ResolveLink(Graph.GetFileName(parentNode));
        TString name;
        Graph.GetFileName(node).GetStr(name);

        const auto moduleIterator = FindModule(state);
        if (moduleIterator == state.end()) {
            YConfErr(KnownBug) << "Can't find parent module in incl fixer for " << name
                               << " Please contact devtools@" << Endl;
            return false;
        }

        TModuleRestorer restorer({Conf, Graph, Modules}, moduleIterator->Node());
        TModule& module = *restorer.RestoreModule();

        TModuleResolver modRes(module, Conf, MakeModuleResolveContext(module, Conf, Graph, UpdIter, IncParserManager.Cache));
        // modRes.Resolver().MutableOptionsWithClear().NoFS = true;

        auto emitConfigureError = [&](const TStringBuf& s) {
            TScopedContext context(module.GetName(), false);
            YConfErr(BadIncl) << s << Endl;
            NEvent::TBadIncl event;
            event.SetFromHere(TString(parentName.GetTargetStr()));
            event.SetInclude(TString(NPath::CutAllTypes(NPath::ResolveLink(name))));
            TRACE(P, event);
        };

        if (!isIncludeDep) {
            auto resolveFile = modRes.ResolveSourcePath(name, module.GetDir(), TModuleResolver::Default);
            if (resolveFile.Empty() || (resolveFile.Root() != NPath::Build)) {
                return enter;
            }
            YDIAG(V) << "Fix: " << name << " -> " << TResolveFileOut(modRes, resolveFile) << Endl;
            if (!ProcessNewNode(state, module, resolveFile, EMNT_NonParsedFile, modRes)) {
                emitConfigureError(TStringBuilder{}
                    << "resolved to non-configured include file : [[imp]]" << name
                    << "[[rst]] included from here: [[imp]]" << parentName << "[[rst]]"
                );
            }
            return enter;
        }

        if (!IsType(name, NPath::Unset)) {
            return enter; // wtf?
        }
        YDIAG(VV) << "trying to resolve include: " << name << " (parent: " << parentName << ")\n";
        TInclude inc(TInclude::EKind::System /* All local ones shall be resolved early */, name);
        TVector<TResolveFile> targets;
        modRes.ResolveSingleInclude(parentName, inc, targets);
        if (targets.empty()) {
            // It has been successfully resolved to nothing (likely to a sysincl).
            YDIAG(VV) << "resolved to nothing" << Endl;
            ResolvedToNothing++;
            UnlinkLastNode = true;
            return false;
        }
        for (const auto& target : targets) {
            YDIAG(VV) << "found: " << TResolveFileOut(modRes, target) << Endl;
            if (target.Root() == NPath::Unset) {
                emitConfigureError(TStringBuilder{}
                    << "could not resolve include file: [[imp]]" << NPath::CutType(name)
                    << "[[rst]] included from here: [[imp]]" << parentName << "[[rst]]"
                );

                return enter;
            }

            YDIAG(V) << "Fix: " << name << " -> " << TResolveFileOut(modRes, target) << " (for " << parentName << ")\n";
            // EMNT_File is OK because if it is a MissingFile then it is already in the graph (?)
            bool processed = ProcessNewNode(state, module, target, EMNT_File, modRes);
            Y_ASSERT(processed);
        }
        return enter;
    }

    bool ProcessNewNode(TState& state, TModule& module, TResolveFile newFile, EMakeNodeType newType, TModuleResolver& modRes) {
        TNodeId newNodeId = Graph.GetNodeById(newType, newFile.GetElemId()).Id();
        bool nodeInGraph = (newNodeId != TNodeId::Invalid);
        bool visitedByUpdIter = nodeInGraph && UpdIter.Nodes.contains(MakeDepsCacheId(*Graph[newNodeId]));

        if (!visitedByUpdIter) {
            if (newType != EMNT_NonParsedFile) {
                Y_ASSERT(newFile.Root() != NPath::Build);
            } else {
                return false;
            }

            (nodeInGraph ? ResolvedToExisting : ResolvedToNew)++;

            SetDefaultIncParser(modRes.GetTargetBuf(newFile), state); // for extract extension targetPath is enough
            newNodeId = UpdIter.RecursiveAddNode(newType, newFile.GetElemId(), &module);
            UnsetDefaultIncParser();
        }

        const TNodeId nodeId = state.TopNode().Id();
        state.Parent()->LocalRepl[nodeId].push_back(newNodeId);
        state.Top().Fixed = true;
        YDIAG(V) << "Fix: " << nodeId << " -> " << newNodeId << Endl;

        const auto* prevCurEnt = CurEnt;
        IterateAll(Graph, newNodeId, state, *this);
        Y_ASSERT(prevCurEnt == CurEnt);

        return true;
    }

    void Leave(TState& state) {
        TBase::Leave(state);
        TIFixStData& stateData = state.Top();

        bool nodeChanged = false;

        if (Y_UNLIKELY(!stateData.LocalRepl.empty())) {
            Graph.ReplaceEdgesWithList(state.TopNode().Id(), stateData.LocalRepl);
            stateData.LocalRepl.clear();
            nodeChanged = true;
        }

        if (IsModuleType(stateData.Node().Value().NodeType)) {
            Y_ASSERT(DontResolveIncludesForModules.size() > 1u);
            DontResolveIncludesForModules.pop();
            UpdIter.ResolveCaches.Drop(state.TopNode()->ElemId);
        }

        if (UnlinkLastNode) {
            state.IncomingDep().Delete();
            UnlinkLastNode = false;
            nodeChanged = true;
        }

#ifdef NDEBUG
        constexpr bool debug = false;
#else
        constexpr bool debug = true;
#endif
        if (debug || nodeChanged) {
            // Remove duplicate EDT_Include edges between two source files.
            // It is assumed that before this function include edges are unique, so only uniqualize only if needed
            // In debug mode ensure the above invariant via explicit check
            auto edges = state.TopNode().Edges();
            THashSet<TNodeId> seenNodes(edges.Total());
            for(auto e : edges) {
                if (!IsSrcFileType(e.To()->NodeType) || !IsSrcFileType(e.From()->NodeType) || *e != EDT_Include) {
                    continue;
                }
                auto [_, added] = seenNodes.insert(e.To().Id());
                if (debug && !nodeChanged) {
                    Y_ASSERT(added);
                }
                if (!added) {
                    e.Delete();
                }
            }
        }

        if (nodeChanged) {
            stateData.Node()->State.UpdateLocalChanges(true, false);
        }
    }

    bool AcceptDep(TState& state) {
        const auto curDep = state.NextDep();
        EMakeNodeType targetType = curDep.To()->NodeType;
        if (targetType == EMNT_MissingFile) {
            return true;
        }

        const EDepType depType = curDep.Value();
        if (depType == EDT_Search || depType == EDT_Property) {
            return false;
        }

        if (depType == EDT_Search2 && !IsGlobalSrcDep(curDep)) {
            return false;
        }

        if (targetType == EMNT_MakeFile) {
            return false;
        }

        return TBase::AcceptDep(state);
    }

    void SetDefaultIncParser(TStringBuf newName, const TState& state) {
        if (IncParserManager.HasParserFor(newName)) {
            return;
        }
        Y_ASSERT(!state.IsEmpty());
        state.FindRecent(state.begin() + 1, [this](const auto& item) {
            if (!item.Fixed && (!IsSrcFileType(item.Node()->NodeType) || *item.CurDep() != EDT_Include)) {
                return true;
            }
            TFileView nodeName = item.GetFileName();
            if (IncParserManager.HasParserFor(nodeName)) {
                IncParserManager.SetDefaultParserSameAsFor(nodeName);
                return true;
            }
            return false;
        });
    }

    void UnsetDefaultIncParser() {
        IncParserManager.ResetDefaultParser();
    }
};

//XXX: move
void TYMake::FindLostIncludes() {
    NYMake::TTraceStage stage("Find Lost Includes");
    Y_ASSERT(UpdIter != nullptr);
    TIncFixer visitor(Conf, Graph, *UpdIter, Modules, IncParserManager);
    IterateAll(Graph, StartTargets, visitor, [](const TTarget& t) -> bool { return !t.IsNonDirTarget && !t.IsModuleTarget; });
    YDebug() << "TIncFixer stats: "
             << visitor.ResolvedToNothing << " resolved to nothing, "
             << visitor.ResolvedToExisting << " resolved to existing, "
             << visitor.ResolvedToNew << " resolved to new." << Endl;
}
