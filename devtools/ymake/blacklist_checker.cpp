#include "blacklist_checker.h"
#include "builtin_macro_consts.h"
#include "module_state.h"
#include "module_store.h"
#include "module_restorer.h"

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>

namespace {
    class TBaseBlacklistVisitor {
    public:
        TBaseBlacklistVisitor()
            : HasBlacklistErrors_(false)
        {}

        bool HasBlacklistErrors() const noexcept {
            return HasBlacklistErrors_;
        }

    protected:
        bool HasBlacklistErrors_;

        // Make blacklist error, fill scoped context, if need
        void GenerateBlacklistError(
            const TString* ptr,
            const TStringBuf& path,
            const TStringBuf& macro = ""
        ) {
            TString insideMacro;
            if (macro) {
                insideMacro = TStringBuilder() << "inside [[alt1]]" << macro << "[[rst]] ";
            }
            YConfErr(BlckLst) << "Path [[imp]]" << path << "[[rst]] " << insideMacro
                << "is from prohibited directory [[alt1]]" << *ptr << "[[rst]]" << Endl;
            HasBlacklistErrors_ = true; // found some blacklist error during check
        }
    };

    class TBlacklistVisitor: public TDirectPeerdirsVisitor<TVisitorStateItem<TEntryStatsData>, TGraphIteratorStateItemBase<true>>, public TBaseBlacklistVisitor {
    public:
        using TBase = TDirectPeerdirsVisitor<TVisitorStateItem<TEntryStatsData>, TGraphIteratorStateItemBase<true>>;
        using TState = TBase::TState;

        TBlacklistVisitor(const TRestoreContext& restoreContext)
            : TBaseBlacklistVisitor()
            , RestoreContext_(restoreContext)
            , IsBlacklistHashChanged_(restoreContext.Conf.IsBlacklistHashChanged())
        {}

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }
            const auto topNode = state.TopNode();
            auto topNodeType = topNode->NodeType;
            if (IsModuleType(topNodeType)) {
                const auto* module = RestoreContext_.Modules.Get(topNode->ElemId);
                CheckModule(module);
            } else if (IsSrcFileType(topNodeType)) {
                CheckPath(RestoreContext_.Graph.GetFileName(topNode->ElemId), state);
            } else if (IsMakeFileType(topNodeType)) {
                auto pathView = RestoreContext_.Graph.GetFileName(topNode->ElemId);
                if (!pathView.GetTargetStr().EndsWith("/ya.make")) { // skip ya.make files - they duplicate checks by directory
                    CheckPath(pathView, state);
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


    private:
        const TRestoreContext& RestoreContext_;
        const bool IsBlacklistHashChanged_;

        // Check all module directories (include module directory, when need)
        void CheckModule(const TModule* module) {
            if (!RequireModuleRecheck(module)) {
                return;
            }

            const auto moduleDirView = module->GetDir();

            YConfEraseByOwner(BlckLst, module->GetId()); // clear all existing module blacklist errors

            THolder<TScopedContext> scopedContext{nullptr};
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
        }

        void CheckModuleDir(const TModule* module, THolder<TScopedContext>& scopedContext, const TFileView dirView, const TStringBuf& macro = "") {
            auto dir = dirView.GetTargetStr();
            if (const auto* ptr = RestoreContext_.Conf.BlackList.IsValidPath(dir)) {
                if (!scopedContext) {
                    scopedContext = MakeHolder<TScopedContext>(
                        RestoreContext_.Graph.Names().FileConf.GetStoredName(
                            NPath::SmartJoin(module->GetDir().GetTargetStr(), "ya.make")
                        )
                    );
                }
                GenerateBlacklistError(ptr, dir, macro);
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
                if (moduleIt != state.end()) { // if module exists in state, fill it to context
                    scopedContext = MakeHolder<TScopedContext>(RestoreContext_.Modules.Get(moduleIt->Node()->ElemId)->GetName());
                } else {
                    const auto& parentNode = state.ParentNode();
                    if (parentNode.IsValid()) { // else fill parent node to context
                        scopedContext = MakeHolder<TScopedContext>(RestoreContext_.Graph.GetFileName(parentNode->ElemId));
                    }
                }
                GenerateBlacklistError(ptr, path, macro);
            }
        }

        bool RequireModuleRecheck(const TModule* module) {
            // Recheck require if blacklist changed OR module reconstructed
            // OR module has some errors in cache (some files may be removed, we must clear errors for they)
            // ELSE module blacklist errors must be valid from cache
            return IsBlacklistHashChanged_ || !module->IsLoaded() || YConfHasMessagesByOwner(BlckLst, module->GetId());
        }
    };

    class TRecurseBlacklistVisitor: public TNoReentryStatsConstVisitor<TVisitorStateItem<TEntryStatsData>>, public TBaseBlacklistVisitor {
    public:
        using TBase = TNoReentryStatsConstVisitor<TVisitorStateItem<TEntryStatsData>>;
        using TState = typename TBase::TState;

        TRecurseBlacklistVisitor(const TRestoreContext& restoreContext)
            : TBaseBlacklistVisitor()
            , RestoreContext_(restoreContext)
        {}

        bool AcceptDep(TState& state) {
            bool result = TBase::AcceptDep(state);
            return result && !IsModuleType(state.NextDep().To()->NodeType);
        }

        bool Enter(TState& state) {
            const auto& topNode = state.TopNode();
            if (!TBase::Enter(state) || !IsDirType(topNode->NodeType)) {
                return false;
            }
            auto& names = RestoreContext_.Graph.Names();
            auto path = names.FileNameById(topNode->ElemId).GetTargetStr();
            if (const auto* ptr = RestoreContext_.Conf.BlackList.IsValidPath(path)) {
                const auto& parentNode = state.ParentNode();
                TStringBuf dir;
                TStringBuf macro;
                if (parentNode.IsValid()) {
                    const auto depType = state.IncomingDep().Value();
                    switch (depType) {
                        case EDT_Include:{
                            static const TString SOME_RECURSE = Join('/', NMacro::RECURSE, NMacro::RECURSE_ROOT_RELATIVE);
                            macro = SOME_RECURSE;
                        }; break;
                        case EDT_BuildFrom: macro = NProps::DEPENDS; break;
                        case EDT_Search: macro = NMacro::RECURSE_FOR_TESTS; break;
                        default: macro = "some recurse like macro";
                    }
                    dir = names.FileNameById(parentNode->ElemId).GetTargetStr();
                } else {
                    dir = names.FileNameById(topNode->ElemId).GetTargetStr(); // if no parent use my ya.make as context
                    macro = "command arguments";
                }
                auto scopedContext = MakeHolder<TScopedContext>(names.FileConf.GetStoredName(NPath::SmartJoin(dir, "ya.make")));
                GenerateBlacklistError(ptr, path, macro);
            }
            return true;
        }

    private:
        const TRestoreContext& RestoreContext_;
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
    TRecurseBlacklistVisitor recurseBlacklistVisitor(RecurseRestoreContext_);
    IterateAll(RecurseRestoreContext_.Graph, RecurseStartTargets_, recurseBlacklistVisitor);
    TBlacklistVisitor blacklistVisitor(RestoreContext_);
    IterateAll(RestoreContext_.Graph, StartTargets_, blacklistVisitor);
    return !blacklistVisitor.HasBlacklistErrors() && !recurseBlacklistVisitor.HasBlacklistErrors();
}
