#include "json_entry_stats.h"

#include "json_saveload.h"

TJsonStatsBase::~TJsonStatsBase() noexcept {
}

TJsonStatsOld::TJsonStatsOld(TNodeDebugOnly nodeDebug)
    : ContextSign{nodeDebug, "TJSONEntryStats::ContextSign"sv}
    , IncludedContextSign{nodeDebug, "TJSONEntryStats::IncludedContextSign"sv}
    , SelfContextSign{nodeDebug, "TJSONEntryStats::SelfContextSign"sv}
    , IncludedSelfContextSign{nodeDebug, "TJSONEntryStats::IncludedSelfContextSign"sv}
    , RenderId{nodeDebug, "TJSONEntryStats::RenderId"sv}
{
}

void TJsonStatsOld::SetContextSign(const TMd5SigValue& md5, TUidDebugNodeId id) {
    ContextSign = md5;
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set ContextSign, value is " << ContextSign.ToBase64() << Endl;
}

void TJsonStatsOld::SetContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt) {
    TMd5Value md5 = oldMd5;
    md5.Update(contextSalt, "TJSONEntryStats::SetContextSign::<contextSalt>"sv);
    ContextSign.MoveFrom(std::move(md5));
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set ContextSign, value is " << ContextSign.ToBase64() << Endl;
}

void TJsonStatsOld::SetIncludedContextSign(const TMd5SigValue& md5) {
    IncludedContextSign = md5;
    YDIAG(Dev) << "Set IncludedContextSign, value is " << IncludedContextSign.ToBase64() << Endl;
}

void TJsonStatsOld::SetIncludedContextSign(const TMd5Value& oldMd5) {
    IncludedContextSign.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set IncludedContextSign, value is " << IncludedContextSign.ToBase64() << Endl;
}

void TJsonStatsOld::SetSelfContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt) {
    TMd5Value md5 = oldMd5;
    md5.Update(contextSalt, "TJSONEntryStats::SetSelfContextSign::<contextSalt>"sv);
    SelfContextSign.MoveFrom(std::move(md5));
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogSelfContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set SelfContextSign, value is " << SelfContextSign.ToBase64() << Endl;
}

void TJsonStatsOld::SetSelfContextSign(const TMd5SigValue& md5, TUidDebugNodeId id) {
    SelfContextSign = md5;
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogSelfContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set SelfContextSign, value is " << SelfContextSign.ToBase64() << Endl;
}

TJsonStatsNew::TJsonStatsNew(TNodeDebugOnly nodeDebug)
    : StructureUID(nodeDebug, "TJSONEntryStats::StructureUID"sv)
    , IncludeStructureUID(nodeDebug, "TJSONEntryStats::IncludeStructureUID"sv)
    , ContentUID(nodeDebug, "TJSONEntryStats::ContentUID"sv)
    , IncludeContentUID(nodeDebug, "TJSONEntryStats::IncludeContentUID"sv)
    , FullUID(nodeDebug, "TJSONEntryStats::FullUID"sv)
    , SelfUID(nodeDebug, "TJSONEntryStats::SelfUID"sv)
    , IsFullUIDCompleted(false)
    , IsSelfUIDCompleted(false)
{}

void TJsonStatsNew::SetStructureUid(const TMd5SigValue& md5) {
    StructureUID = md5;
    YDIAG(Dev) << "Set StructureUID, value is " << StructureUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetStructureUid(const TMd5Value& oldMd5) {
    StructureUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set StructureUID, value is " << StructureUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetIncludeStructureUid(const TMd5SigValue& md5) {
    IncludeStructureUID = md5;
    YDIAG(Dev) << "Set IncludeStructureUID, value is " << IncludeStructureUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetIncludeStructureUid(const TMd5Value& oldMd5) {
    IncludeStructureUID.CopyFrom(oldMd5);
}

void TJsonStatsNew::SetContentUid(const TMd5SigValue& md5) {
    ContentUID = md5;
    YDIAG(Dev) << "Set ContentUID, value is " << ContentUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetContentUid(const TMd5Value& oldMd5) {
    ContentUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set ContentUID, value is " << ContentUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetIncludeContentUid(const TMd5SigValue& md5) {
    IncludeContentUID = md5;
    YDIAG(Dev) << "Set IncludeContentUID, value is " << IncludeContentUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetIncludeContentUid(const TMd5Value& oldMd5) {
    IncludeContentUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set ContentUID, value is " << IncludeContentUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetFullUid(const TMd5Value& oldMd5) {
    FullUID.CopyFrom(oldMd5);
    IsFullUIDCompleted = true;
    YDIAG(Dev) << "Set FullUID, value is " << FullUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetFullUid(const TMd5SigValue& oldMd5) {
    FullUID = oldMd5;
    IsFullUIDCompleted = true;
    YDIAG(Dev) << "Set FullUID, value is " << FullUID.ToBase64() << Endl;
}

void TJsonStatsNew::SetSelfUid(const TMd5Value& oldMd5) {
    SelfUID.CopyFrom(oldMd5);
    IsSelfUIDCompleted = true;
    YDIAG(Dev) << "Set SelfUID, value is " << SelfUID.ToBase64() << Endl;
}

TJSONEntryStats::TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack, bool isFile)
    : TEntryStats(nodeDebug, inStack, isFile)
    , TNodeDebugOnly{nodeDebug}
    , AllFlags(0)
    , IsGlobalVarsCollectorStarted(false)
{
}

TString TJSONEntryStats::GetNodeUid(bool newUids) const {
    if (newUids)
        return NewUids()->GetNodeUid();
    else
        return OldUids()->GetNodeUid();
}

TString TJSONEntryStats::GetNodeSelfUid(bool newUids) const {
    if (newUids)
        return NewUids()->GetNodeSelfUid();
    else
        return OldUids()->GetNodeSelfUid();
}

void TJSONEntryStats::Save(TSaveBuffer* buffer, const TDepGraph& graph, bool newUids) const noexcept {
    buffer->Save<ui8>(static_cast<const TEntryStatsData*>(this)->AllFlags);
    buffer->Save<ui8>(AllFlags);
    buffer->SaveElemId(OutTogetherDependency, graph);
    buffer->SaveElemIds(IncludedDeps, graph);
    buffer->SaveElemIds(NodeDeps, graph);
    buffer->SaveElemIds(NodeToolDeps, graph);
    buffer->SaveElemIds(ExtraOuts, graph);
    buffer->SaveReservedVars(UsedReservedVars.Get(), graph);

    if (newUids) {
        buffer->Save(NewUids()->StructureUID.GetRawData(), 16);
        buffer->Save(NewUids()->IncludeStructureUID.GetRawData(), 16);
        buffer->Save(NewUids()->ContentUID.GetRawData(), 16);
        buffer->Save(NewUids()->IncludeContentUID.GetRawData(), 16);
        buffer->Save(NewUids()->FullUID.GetRawData(), 16);
        buffer->Save(NewUids()->SelfUID.GetRawData(), 16);
    } else {
        buffer->Save(OldUids()->IncludedContextSign.GetRawData(), 16);
        buffer->Save(OldUids()->ContextSign.GetRawData(), 16);
        buffer->Save(OldUids()->SelfContextSign.GetRawData(), 16);
        buffer->Save(OldUids()->IncludedSelfContextSign.GetRawData(), 16);
        buffer->Save(OldUids()->RenderId.GetRawData(), 16);
    }
}

bool TJSONEntryStats::Load(TLoadBuffer* buffer, const TDepGraph& graph, bool newUids) noexcept {
    static_cast<TEntryStatsData*>(this)->AllFlags = buffer->Load<ui8>();
    AllFlags = buffer->Load<ui8>();
    if (!buffer->LoadElemId(&OutTogetherDependency, graph))
        return false;
    if (!buffer->LoadElemIds(&IncludedDeps, graph))
        return false;
    if (!buffer->LoadElemIds(&NodeDeps, graph))
        return false;
    if (!buffer->LoadElemIds(&NodeToolDeps, graph))
        return false;
    if (!buffer->LoadElemIds(&ExtraOuts, graph))
        return false;
    if (!buffer->LoadReservedVars(&UsedReservedVars, graph))
        return false;

    if (newUids) {
        buffer->LoadMd5(&NewUids()->StructureUID);
        buffer->LoadMd5(&NewUids()->IncludeStructureUID);
        buffer->LoadMd5(&NewUids()->ContentUID);
        buffer->LoadMd5(&NewUids()->IncludeContentUID);
        buffer->LoadMd5(&NewUids()->FullUID);
        buffer->LoadMd5(&NewUids()->SelfUID);

        NewUids()->IsSelfUIDCompleted = true;
        NewUids()->IsFullUIDCompleted = true;

        NewUids()->Finished = true;
    } else {
        buffer->LoadMd5(&OldUids()->IncludedContextSign);
        buffer->LoadMd5(&OldUids()->ContextSign);
        buffer->LoadMd5(&OldUids()->SelfContextSign);
        buffer->LoadMd5(&OldUids()->IncludedSelfContextSign);
        buffer->LoadMd5(&OldUids()->RenderId);
    }

    HasUsualEntry = false;
    WasVisited = false;
    WasFresh = false;

    Completed = true;

    return true;
}
