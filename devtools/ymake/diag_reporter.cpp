#include <devtools/ymake/peers.h>
#include <devtools/ymake/ymake.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag_reporter.h>

namespace {

    NEvent::TForeignPlatformTarget MakeForeignPlatformTargetEvent(::NEvent::TForeignPlatformTarget::EPlatform platform, NEvent::TForeignPlatformTarget::EKind kind, const TModule& mod) {
        NEvent::TForeignPlatformTarget res;
        res.SetPlatform(platform);
        res.SetReachable(kind);
        res.SetDir(TString{mod.GetDir().CutType()});
        res.SetModuleTag(TString{mod.GetTag()});
        return res;
    }

    NEvent::TForeignPlatformTarget RequiredToolEvent(const TModule& mod) {
        return MakeForeignPlatformTargetEvent(::NEvent::TForeignPlatformTarget::TOOL, ::NEvent::TForeignPlatformTarget::REQUIRED, mod);
    }

    NEvent::TForeignPlatformTarget RequiredPicEvent(const TModule& mod) {
        return MakeForeignPlatformTargetEvent(::NEvent::TForeignPlatformTarget::PIC, ::NEvent::TForeignPlatformTarget::REQUIRED, mod);
    }

    NEvent::TForeignPlatformTarget RequiredNoPicEvent(const TModule& mod) {
        return MakeForeignPlatformTargetEvent(::NEvent::TForeignPlatformTarget::NOPIC, ::NEvent::TForeignPlatformTarget::REQUIRED, mod);
    }

    NEvent::TForeignPlatformTarget RequiredIDEDependEvent(const TModule& mod) {
        return MakeForeignPlatformTargetEvent(::NEvent::TForeignPlatformTarget::IDE_DEPEND, ::NEvent::TForeignPlatformTarget::REQUIRED, mod);
    }
}

bool TForeignPlatformEventsReporter::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    const auto& node = state.TopNode();

    if (ReportPicNopic && TransitionSource != ETransition::None && IsModule(state.Top())) {
        auto module = Modules.Get(node->ElemId);
        if (module->Transition != ETransition::None && module->Transition != TransitionSource) {
            if (module->Transition == ETransition::Pic) {
                Writer.WriteLine(NYMake::EventToStr(RequiredPicEvent(*Modules.Get(node->ElemId))));
            } else if (module->Transition == ETransition::NoPic) {
                Writer.WriteLine(NYMake::EventToStr(RequiredNoPicEvent(*Modules.Get(node->ElemId))));
            }
        }
    }

    if (fresh) {
        if (IsModule(state.Top())) {
            if (RenderSemantics) {
                auto module = Modules.Get(node->ElemId);
                if (module->IsSemForeign()) {
                    FORCE_TRACE(T, RequiredIDEDependEvent(*module));
                }
                if (module->IsSemIgnore()) {
                    return false;
                }
            }
        } else {
            // Avoid every-visit processing on non-Module nodes
            CurEnt->ToolReported = true;
        }

        if (IsMakeFile(state.Top())) {
            return false;
        }
    }

    // We should report tool even if we visited node by non-tool edge first
    if (!CurEnt->ToolReported && state.HasIncomingDep() && IsModule(state.Top()) && IsDirectToolDep(state.IncomingDep())) {
        if (!TDepGraph::Graph(node).Names().CommandConf.GetById(TVersionedCmdId(state.IncomingDep().From()->ElemId).CmdId()).KeepTargetPlatform) {
            Writer.WriteLine(NYMake::EventToStr(RequiredToolEvent(*Modules.Get(node->ElemId))));
            CurEnt->ToolReported = true;
        }
    }

    return fresh || state.Top().IsStart;
}

bool TForeignPlatformEventsReporter::AcceptDep(TState& state) {
    const auto& dep = state.NextDep();
    const EDepType depType = dep.Value();

    if (depType == EDT_Search || (depType == EDT_Search2 && !IsGlobalSrcDep(dep)) ||
        depType == EDT_Property || depType == EDT_OutTogetherBack || IsRecurseDep(dep) || IsDirToModuleDep(dep)) { // Don't follow. Use direct Peerdirs/Tooldirs for walking
        return false;
    }

    return TBase::AcceptDep(state);
}

bool TDupSrcReporter::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    const auto& node = state.TopNode();

    if (fresh) {
        if (IsModule(state.Top())) {
            auto module = Modules.Get(node->ElemId);
            if (RenderSemantics) {
                if (module->IsSemIgnore()) {
                    return false;
                }
            }

            CurEnt->WasFresh = true;
            Diag()->Where.push_back(module->GetName().GetElemId(), module->GetName().GetTargetStr());
        }

        if (IsMakeFile(state.Top())) {
            return false;
        }

        if (state.HasIncomingDep()) {
            if (const auto incDep = state.IncomingDep(); IsModuleOwnNodeDep(incDep)) {
                ConfMsgManager()->AddDupSrcLink(node->ElemId, Diag()->Where.back().first, false);
            }
        }
    }

    return fresh || state.Top().IsStart;
}

bool TDupSrcReporter::AcceptDep(TState& state) {
    const auto& dep = state.NextDep();
    const EDepType depType = dep.Value();

    if (depType == EDT_Search || (depType == EDT_Search2 && !IsGlobalSrcDep(dep)) ||
        depType == EDT_Property || depType == EDT_OutTogetherBack || IsRecurseDep(dep) || IsDirToModuleDep(dep)) { // Don't follow. Use direct Peerdirs/Tooldirs for walking
        return false;
    }

    return TBase::AcceptDep(state);
}

void TDupSrcReporter::Leave(TState& state) {
    TBase::Leave(state);

    if (CurEnt->WasFresh && IsModule(state.Top())) {
        CurEnt->WasFresh = false;
        Diag()->Where.pop_back();
    }
}

void TYMake::ReportForeignPlatformEvents() {
    NYMake::TTraceStage scopeTracer{"Report Foreign Platform Events"};
    TForeignPlatformEventsReporter eventReporter(Names, Modules, Conf.RenderSemantics, Conf.TransitionSource, Conf.ReportPicNoPic, *Conf.ForeignTargetWriter);
    IterateAll(Graph, StartTargets, eventReporter, [](const TTarget& t) -> bool {
        return t.IsModuleTarget;
    });
    Conf.ForeignTargetWriter->WriteLine(NYMake::EventToStr(NEvent::TAllForeignPlatformsReported{}));
}

void FlushModuleNode(TConstDepNodeRef modNode) {
    ui32 elemId = modNode->ElemId;
    TScopedContext context(TDepGraph::Graph(modNode).GetFileName(elemId));
    ConfMsgManager()->Flush(elemId);
    ConfMsgManager()->AddVisitedModule(elemId);
}

void FlushMakeFileNode(TConstDepNodeRef makeFileNode, const TSymbols& names) {
    const auto view = names.FileConf.GetName(makeFileNode->ElemId);
    const auto targetView = names.FileConf.ResolveLink(view);
    TScopedContext context(targetView);
    ConfMsgManager()->Flush(view.GetElemId());
    ConfMsgManager()->Flush(targetView.GetElemId());
}

void TYMake::ReportConfigureEvents() {
    NYMake::TTraceStage scopeTracer{"Report Configure Events"};

    ConfMsgManager()->FlushTopLevel();

    for (const auto& node : Graph.Nodes()) {
        if (!node->State.GetReachable() ) {
            continue;
        }

        if (IsModuleType(node->NodeType)) {
            if (Conf.RenderSemantics && Conf.ForeignOnNoSem) {
                const auto elemId = node->ElemId;
                const auto& module = Modules.Get(elemId);
                if (module->IsSemIgnore()) {
                    YConfEraseByOwner(NoSem, elemId);
                }
            }
            FlushModuleNode(node);
        }

        if (IsMakeFileType(node->NodeType)) {
            FlushMakeFileNode(node, Names);
        }
    }

    TDupSrcReporter dupSrcReporter(Modules, Conf.RenderSemantics);
    IterateAll(Graph, StartTargets, dupSrcReporter, [](const TTarget& t) -> bool {
        return t.IsModuleTarget;
    });

    ConfMsgManager()->ReportDupSrcConfigureErrors([this](ui32 id) { return Names.FileConf.GetName(id).GetTargetStr(); });
}
