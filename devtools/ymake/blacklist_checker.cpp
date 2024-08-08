#include "blacklist_checker.h"
#include "builtin_macro_consts.h"
#include "module_state.h"
#include "module_store.h"
#include "module_restorer.h"

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>

namespace {
    class TBlacklistVisitor: public TDirectPeerdirsVisitor<TVisitorStateItem<TEntryStatsData>, TGraphIteratorStateItemBase<true>> {
    public:
        using TBase = TDirectPeerdirsVisitor<TVisitorStateItem<TEntryStatsData>, TGraphIteratorStateItemBase<true>>;
        using TState = TBase::TState;

        TBlacklistVisitor(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets)
            : RestoreContext_(restoreContext)
            , StartTargets_(startTargets)
            , IsBlacklistHashChanged_(restoreContext.Conf.IsBlacklistHashChanged())
            , HasBlacklistErrors_(false)
            , DirectoryFound_(0)
        {}

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }
            const auto topNode = state.TopNode();
            auto topNodeType = topNode->NodeType;
            if (IsModuleType(topNodeType)) {
                CheckModule(RestoreContext_.Modules.Get(topNode->ElemId), topNode);
            } else if (IsSrcFileType(topNodeType) || IsMakeFileType(topNodeType)) {
                CheckPath(RestoreContext_.Graph.GetFileName(topNode->ElemId), state);
            } else if (IsDirType(topNodeType)) {
                if (DirectoryFound_++) { // first directory is start directory
                    // Other directories is RECURSE or DEPENDS from start directory
                    // If directory has module in edges, it will be checked as module directory later, skip check it here
                    if (FindIf(topNode.Edges(), [](const auto& edge) { return IsModuleType(edge.To()->NodeType); }) == topNode.Edges().end()) {
                        CheckPath(RestoreContext_.Graph.GetFileName(topNode->ElemId), state, SomeRecurse());
                    }
                }
            }
            return true;
        }

        bool AcceptDep(TState& state) {
            if (!TBase::AcceptDep(state)) {
                return false;
            }
            return !IsPropertyDep(state.NextDep()); // skip only properties
        }

        bool HasBlacklistErrors() const noexcept {
            return HasBlacklistErrors_;
        }

    private:
        const TRestoreContext& RestoreContext_;
        const TVector<TTarget>& StartTargets_;
        const bool IsBlacklistHashChanged_;
        bool HasBlacklistErrors_;
        int DirectoryFound_;

        // Check all module directories (include module directory, when need)
        void CheckModule(const TModule* module, TState::TNodeRef moduleNode) {
            if (!RequireModuleRecheck(module)) {
                return;
            }

            const auto moduleDirView = module->GetDir();

            YConfEraseByOwner(BlckLst, module->GetId()); // clear all existing module blacklist errors
            THolder<TScopedContext> scopedContext{nullptr};

            if (module->IsStartTarget()) {
                // Check module directory only for start targets, other checked as Peers
                const auto startTargetIt = FindIf(StartTargets_, [moduleNodeId=moduleNode.Id()](const TTarget& target){
                    return target.Id == moduleNodeId;
                });
                if (startTargetIt != StartTargets_.end() && !startTargetIt->IsDependsTarget) { // skip DEPENDS start targets, it checked as module DEPENDS
                    CheckModuleDir(module, scopedContext, moduleDirView,
                        startTargetIt->IsDepTestTarget // detect macros by start target flags
                            ? NMacro::RECURSE_FOR_TESTS
                            : (startTargetIt->IsRecurseTarget
                                ? SomeRecurse()
                                : ""
                            )
                    );
                }
            }

            for (const auto dirView : module->Peers) {
                CheckModuleDir(module, scopedContext, dirView, NMacro::PEERDIR);
            }

            for (const auto& [dirElemId, _] : module->GhostPeers) {
                CheckModuleDir(module, scopedContext, RestoreContext_.Graph.GetFileName(dirElemId), "DEPENDENCY_MANAGEMENT");
            }

            for (const auto dirView : module->SrcDirs) {
                if (dirView == moduleDirView) {
                    continue; // module directories checked by another way
                }
                CheckModuleDir(module, scopedContext, dirView, NMacro::SRCDIR);
            }

            for (const auto& [langId, langIncDirs] : module->IncDirs.GetAll()) {
                if (langIncDirs.Global) {
                    for (const auto dirView : *langIncDirs.Global) {
                        CheckModuleDir(module, scopedContext, dirView, NMacro::ADDINCL);
                    }
                }
                if (langIncDirs.UserGlobal) {
                    for (const auto dirView : *langIncDirs.UserGlobal) {
                        CheckModuleDir(module, scopedContext, dirView, NMacro::ADDINCL);
                    }
                }
                if (langIncDirs.LocalUserGlobal) {
                    for (const auto dirView : *langIncDirs.LocalUserGlobal) {
                        CheckModuleDir(module, scopedContext, dirView, NMacro::ADDINCL);
                    }
                }
            }

            if (module->DataPaths) {
                for (const auto dirView : *module->DataPaths) {
                    CheckModuleDir(module, scopedContext, dirView, "DATA/DATA_FILES");
                }
            }

            if (module->Depends) {
                for (const auto dirView : *module->Depends) {
                    CheckModuleDir(module, scopedContext, dirView, NProps::DEPENDS);
                }
            }
        }

        void CheckModuleDir(const TModule* module, THolder<TScopedContext>& scopedContext, const TFileView dirView, const TStringBuf& macro = "") {
            auto dir = dirView.GetTargetStr();
            if (const auto* ptr = RestoreContext_.Conf.BlackList.IsValidPath(dir)) {
                GenerateBlacklistError(module, scopedContext, ptr, dir, macro);
            }
        }

        void CheckPath(TFileView pathView, TState& state, const TStringBuf& macro = "") {
            pathView = RestoreContext_.Graph.Names().FileConf.ResolveLink(pathView);
            auto path = pathView.GetTargetStr();
            if (NPath::GetType(path) != NPath::Source) {
                return; // ignore non-source
            }
            if (const auto* ptr = RestoreContext_.Conf.BlackList.IsValidPath(path)) {
                THolder<TScopedContext> scopedContext{nullptr};
                const auto moduleIt = FindModule(state);
                const auto* module = moduleIt != state.end()
                    ? RestoreContext_.Modules.Get(moduleIt->Node()->ElemId)
                    : nullptr;
                GenerateBlacklistError(module, scopedContext, ptr, path, macro);
            }
        }

        // Make blacklist error, fill scoped context, if need
        void GenerateBlacklistError(
            const TModule* module,
            THolder<TScopedContext>& scopedContext,
            const TString* ptr,
            const TStringBuf& path,
            const TStringBuf& macro = ""
        ) {
            if (!scopedContext && module) {
                scopedContext.Reset(new TScopedContext(module->GetName()));
            }
            TString insideMacro;
            if (macro) {
                insideMacro = TStringBuilder() << "inside [[alt1]]" << macro << "[[rst]] ";
            }
            YConfErr(BlckLst) << "Path [[imp]]" << path << "[[rst]] " << insideMacro
                << "is from prohibited directory [[alt1]]" << *ptr << "[[rst]]" << Endl;
            HasBlacklistErrors_ = true; // found some blacklist error during check
        }

        bool RequireModuleRecheck(const TModule* module) {
            // Recheck require if blacklist changed OR module reconstructed
            // OR module has some errors in cache (some files may be removed, we must clear errors for they)
            // ELSE module blacklist errors must be valid from cache
            return IsBlacklistHashChanged_ || !module->IsLoaded() || YConfHasMessagesByOwner(BlckLst, module->GetId());
        }

        const TString& SomeRecurse() const {
            static const TString SOME_RECURSE = TString{NMacro::RECURSE} + "/" + TString{NMacro::RECURSE_ROOT_RELATIVE};
            return SOME_RECURSE;
        }
    };
}

bool TBlacklistChecker::HasBlacklist() const {
    return !RestoreContext_.Conf.BlackList.Empty();
}

bool TBlacklistChecker::CheckAll() {
    if (!HasBlacklist()) {
        if (RestoreContext_.Conf.IsBlacklistHashChanged()) {
            // Was some blacklist, but now it empty, we must clear all blacklist errors
            YConfErase(BlckLst);
        }
        return true; // no blacklist, do nothing
    }
    TBlacklistVisitor blacklistVisitor(RestoreContext_, StartTargets_);
    IterateAll(RestoreContext_.Graph, StartTargets_, blacklistVisitor);
    return !blacklistVisitor.HasBlacklistErrors();
}
