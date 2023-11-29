#include <devtools/ymake/peers.h>
#include <devtools/ymake/ymake.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag_reporter.h>

namespace {

    NEvent::TForeignPlatformTarget RequiredTool(const TModule& mod) {
        NEvent::TForeignPlatformTarget res;
        res.SetPlatform(::NEvent::TForeignPlatformTarget::TOOL);
        res.SetReachable(::NEvent::TForeignPlatformTarget::REQUIRED);
        res.SetDir(TString{mod.GetDir().CutType()});
        res.SetModuleTag(TString{mod.GetTag()});
        return res;
    }

}

void TConfigureEventsReporter::PushModule(TConstDepNodeRef modNode) {
    CurEnt->PrevModule = CurModule;
    ui32 elemId = modNode->ElemId;
    CurModule = Modules.Get(elemId);
    Y_ASSERT(CurModule != nullptr);
    Y_ASSERT(CurModule->GetId() == elemId);
    CurEnt->WasFresh = true;

    TStringBuf moduleName = TDepGraph::Graph(modNode).GetFileName(elemId).GetTargetStr();
    Diag()->Where.push_back(elemId, moduleName);
    ConfMsgManager()->Flush(elemId);
    ConfMsgManager()->AddVisitedModule(elemId);
}

void TConfigureEventsReporter::PopModule() {
    CurModule = CurEnt->PrevModule;
    CurEnt->WasFresh = false;
    Diag()->Where.pop_back();
}

bool TConfigureEventsReporter::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    const auto& node = state.TopNode();

    if (fresh) {
        if (IsModule(state.Top())) {
            if (RenderSemantics) {
                auto module = Modules.Get(node->ElemId);
                if (module->IsSemIgnore()) {
                    return false;
                }
            }
            PushModule(node);
        } else {
            // Avoid every-visit processing on non-Module nodes
            CurEnt->ToolReported = true;
        }

        if (IsMakeFile(state.Top())) {
            const auto view = Names.FileConf.GetName(node->ElemId);
            const auto targetView = Names.FileConf.ResolveLink(view);
            TScopedContext context(targetView);
            ConfMsgManager()->Flush(view.GetElemId());
            ConfMsgManager()->Flush(targetView.GetElemId());
            return false;
        }

        if (state.HasIncomingDep()) {
            if (const auto incDep = state.IncomingDep(); IsModuleOwnNodeDep(incDep)) {
                ConfMsgManager()->AddDupSrcLink(node->ElemId, Diag()->Where.back().first, false);
            }
        }
    }

    // We should report tool even if we visited node by non-tool edge first
    if (!CurEnt->ToolReported && state.HasIncomingDep() && IsModule(state.Top()) && IsDirectToolDep(state.IncomingDep())) {
        if (!TDepGraph::Graph(node).Names().CommandConf.GetById(TVersionedCmdId(state.IncomingDep().From()->ElemId).CmdId()).KeepTargetPlatform) {
            FORCE_TRACE(T, RequiredTool(*Modules.Get(node->ElemId)));
            CurEnt->ToolReported = true;
        }
    }

    return fresh || state.Top().IsStart;
}

bool TConfigureEventsReporter::AcceptDep(TState& state) {
    const auto& dep = state.NextDep();
    const EDepType depType = dep.Value();

    if (depType == EDT_Search || (depType == EDT_Search2 && !IsGlobalSrcDep(dep)) ||
        depType == EDT_Property || depType == EDT_OutTogetherBack || IsRecurseDep(dep) || IsDirToModuleDep(dep)) { // Don't follow. Use direct Peerdirs/Tooldirs for walking
        return false;
    }

    return TBase::AcceptDep(state);
}

void TConfigureEventsReporter::Leave(TState& state) {
    TBase::Leave(state);

    if (CurEnt->WasFresh && IsModule(state.Top())) {
        PopModule();
    }
}

bool TRecurseConfigureErrorReporter::AcceptDep(TState& state) {
    bool result = TBase::AcceptDep(state);
    return result && !IsModuleType(state.NextDep().To()->NodeType);
}

bool TRecurseConfigureErrorReporter::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    if (!fresh || IsModuleType(state.TopNode()->NodeType)) {
        return false;
    }
    ui32 elemId = state.TopNode()->ElemId;
    TFileView fileView = Names.FileNameById(elemId);
    TString makefile = NPath::SmartJoin(fileView.GetTargetStr(), "ya.make");
    ui32 targetId = Names.FileConf.GetIdNx(makefile);
    if (targetId != 0) {
        TFileView target = Names.FileConf.GetName(targetId);
        TScopedContext context(target);
        ConfMsgManager()->Flush(target.GetElemId());
        ConfMsgManager()->Flush(TFileConf::ConstructLink(ELT_MKF, target).GetElemId());
    }
    return true;
}

void TYMake::ReportConfigureEvents() {
    NYMake::TTraceStage{"Report Configure Events"};
    TConfigureEventsReporter errorReporter(Names, Modules, Conf.RenderSemantics);
    IterateAll(Graph, StartTargets, errorReporter, [](const TTarget& t) -> bool {
        return t.IsModuleTarget;
    });
    FORCE_TRACE(T, NEvent::TAllForeignPlatformsReported{});

    ConfMsgManager()->ReportDupSrcConfigureErrors([this](ui32 id) { return Names.FileConf.GetName(id).GetTargetStr(); });

    TRecurseConfigureErrorReporter recurseErrorsReporter(Names);
    IterateAll(RecurseGraph, RecurseStartTargets, recurseErrorsReporter);
}
