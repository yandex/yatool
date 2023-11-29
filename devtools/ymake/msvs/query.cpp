#include "query.h"
#include "devtools/ymake/diag/display.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/query.h>

#include <util/string/join.h>

namespace NYMake {
    namespace NMsvs {


        // todo: factor out
        template <class V>
        bool IsToolDep(V& stack) {
            if (stack.Size() < 3) {
                return false;
            }
            const auto& pp = *(stack.begin() + 2);
            return stack.TopNode()->NodeType == EMNT_Program && stack.ParentNode()->NodeType == EMNT_Directory && *pp.NextDep() == EDT_Include && pp.Node()->NodeType == EMNT_BuildCommand;
        }

        void GetTargetsOfType(const TConstDepNodeRef& target, TNodeIds& result, EDepType deptype = (EDepType)-1, EMakeNodeType nodetype = (EMakeNodeType)-1) {
            for (auto dep: target.Edges()) {
                if (((deptype == (EDepType)-1) || (deptype == *dep)) &&
                    ((nodetype == (EMakeNodeType)-1) || (nodetype == dep.To()->NodeType))) {
                    result.push_back(dep.To().Id());
                }
            }
        }

        void GetTargetPeers(const TConstDepNodeRef& target, TNodeIds& result) {
            GetTargetsOfType(target, result, EDT_Include, EMNT_Directory);
            GetTargetsOfType(target, result, EDT_BuildFrom, EMNT_Directory);
        }
        void GetTargetGlobalObjects(const TConstDepNodeRef& target, TNodeIds& result) {
            GetTargetsOfType(target, result, EDT_Search2, EMNT_NonParsedFile);
        }

        void GetTogetherBackChain(const TConstDepNodeRef& file, TNodeIds& result) {
            result.push_back(file.Id());
            for (auto dep: file.Edges()) {
                if (*dep == EDT_OutTogetherBack) {
                    return GetTogetherBackChain(dep.To(), result);
                }
            }
        }

        void GetCommandOutputs(const TConstDepNodeRef& cmd, TNodeIds& result) {
            GetTogetherBackChain(cmd, result);
        }

        template <class TDep>
        void MineCustomFlags(const TDep& dep, TFileFlagsMap& fileFlags, TFileOutSuffixMap& fileOutSuffix) {
            auto cmdView = TDepGraph::GetCmdName(dep.To());
            if (cmdView.IsNewFormat())
                return;
            TStringBuf cmd = cmdView.GetStr();
            if (GetCmdName(cmd).StartsWith("_SRC")) {
                TVector<TStringBuf> args;
                SplitArgs(GetCmdValue(cmd), args);
                if (args.size() > 2) {
                    TNodeId fileId = dep.From().Id();
                    Y_ASSERT(!fileFlags.contains(fileId));
                    auto varIt = std::find(args.begin(), args.end(), "COMPILE_OUT_SUFFIX");
                    if (varIt != args.end()) {
                        auto sufIt = varIt + 1;
                        Y_ASSERT(sufIt != args.end());
                        fileOutSuffix[fileId] = *sufIt;
                        args.erase(varIt, ++sufIt);
                    }
                    fileFlags[fileId] = JoinRange(" ", args.begin() + 2, args.end());
                }
            }
        }

        class TModSrcProc: public TNoReentryStatsConstVisitor<> {
        public:
            using TBase = TNoReentryStatsConstVisitor<>;
            using TBase::Nodes;
            TFileFlagsMap FileFlags;
            TFileOutSuffixMap FileOutSuffix;

            bool SkipGlobals = false;

        public:
            TModSrcProc(bool skipGlobals = false) : SkipGlobals(skipGlobals) {}

            bool AcceptDep(TState& state) {
                auto dep = state.Top().CurDep();
                if (IsBuildCommandDep(dep)) {
                    MineCustomFlags(dep, FileFlags, FileOutSuffix);
                }
                const auto& nextDep = state.NextDep();
                return TBase::AcceptDep(state) && (
                    IsModuleSrcDep(nextDep) ||
                    IsGlobalSrcDep(nextDep) && !SkipGlobals ||
                    IsIndirectSrcDep(nextDep) ||
                    (nextDep.From()->NodeType == EMNT_BuildCommand && *nextDep == EDT_Property && IsFileType(nextDep.To()->NodeType)));
            }
        };

        void CollectModuleSrcs(const TConstDepNodeRef& mod, THashSet<TNodeId>& fnodes, THashSet<TNodeId>* fnodesWithBuildCmd, THashSet<TNodeId>* fnodesGlobal,
                               TFileFlagsMap& fileFlags, TFileOutSuffixMap& fileOutSuffix, bool skipGlobals) {
            TModSrcProc visitor(skipGlobals);
            TGraphConstIteratorState state;
            TDepthGraphIterator<TGraphConstIteratorState, TModSrcProc> it(TDepGraph::Graph(mod), state, visitor);

            THashSet<TNodeId> fileNodes;
            for (bool res = it.Init(mod); res; res = it.Next()) {
                 const auto node = (*it).Node();
                 if (node.Id() != mod.Id()) {
                    if (UseFileId(node->NodeType)) {
                        fileNodes.insert(node.Id());
                        if (fnodesGlobal && state.HasIncomingDep() && IsGlobalSrcDep(state.IncomingDep())) {
                            fnodesGlobal->insert(node.Id());
                        }
                    }
                }
            }
            fnodes.swap(fileNodes);

            if (fnodesWithBuildCmd) {
                for (const auto& f: fnodes) {
                    if (visitor.Nodes.at(f).HasBuildCmd) {
                        fnodesWithBuildCmd->insert(f);
                    }
                }
            }

            fileFlags.swap(visitor.FileFlags);
            fileOutSuffix.swap(visitor.FileOutSuffix);
        }
    }
}
