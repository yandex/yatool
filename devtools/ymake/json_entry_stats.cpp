#include "json_entry_stats.h"

#include "json_saveload.h"
#include "prop_names.h"

TJSONEntryStats::TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack, bool isFile)
    : TJSONEntryStatsNewUID(nodeDebug, inStack, isFile)
    , AllFlags(0)
    , IsGlobalVarsCollectorStarted(false)
    , IncludedContextSign{nodeDebug, "TJSONEntryStats::IncludedContextSign"sv}
    , ContextSign{nodeDebug, "TJSONEntryStats::ContextSign"sv}
    , SelfContextSign{nodeDebug, "TJSONEntryStats::SelfContextSign"sv}
    , IncludedSelfContextSign{nodeDebug, "TJSONEntryStats::IncludedSelfContextSign"sv}
    , RenderId{nodeDebug, "TJSONEntryStats::RenderId"sv}
{
}

TString TJSONEntryStats::GetNodeUid() const {
#if !defined (NEW_UID_IMPL)
    return ContextSign.ToBase64();
#else
    return GetFullUid().ToBase64();
#endif
}

TString TJSONEntryStats::GetNodeSelfUid() const {
#if !defined (NEW_UID_IMPL)
    return SelfContextSign.ToBase64();
#else
    return GetSelfUid().ToBase64();
#endif
}

void TJSONEntryStats::SetIncludedContextSign(const TMd5SigValue& md5) {
    IncludedContextSign = md5;
    YDIAG(Dev) << "Set IncludedContextSign, value is " << IncludedContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::SetIncludedContextSign(const TMd5Value& oldMd5) {
    IncludedContextSign.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set IncludedContextSign, value is " << IncludedContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::SetContextSign(const TMd5SigValue& md5, TUidDebugNodeId id) {
    ContextSign = md5;
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set ContextSign, value is " << ContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::SetContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt) {
    TMd5Value md5 = oldMd5;
    md5.Update(contextSalt, "TJSONEntryStats::SetContextSign::<contextSalt>"sv);
    ContextSign.MoveFrom(std::move(md5));
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set ContextSign, value is " << ContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::SetSelfContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt) {
    TMd5Value md5 = oldMd5;
    md5.Update(contextSalt, "TJSONEntryStats::SetSelfContextSign::<contextSalt>"sv);
    SelfContextSign.MoveFrom(std::move(md5));
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogSelfContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set SelfContextSign, value is " << SelfContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::SetSelfContextSign(const TMd5SigValue& md5, TUidDebugNodeId id) {
    SelfContextSign = md5;
    if (Y_UNLIKELY(Diag()->UIDs)) {
        NUidDebug::LogSelfContextMd5Assign(id, GetNodeUid());
    }
    YDIAG(Dev) << "Set SelfContextSign, value is " << SelfContextSign.ToBase64() << Endl;
}

void TJSONEntryStats::Save(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept {
    buffer->Save<ui8>(static_cast<const TEntryStatsData*>(this)->AllFlags);
    buffer->Save<ui8>(AllFlags);
    buffer->SaveElemId(OutTogetherDependency, graph);
    buffer->SaveElemIds(IncludedDeps, graph);
    buffer->SaveElemIds(NodeDeps, graph);
    buffer->SaveElemIds(NodeToolDeps, graph);
    buffer->SaveElemIds(ExtraOuts, graph);
    buffer->SaveReservedVars(UsedReservedVars.Get(), graph);

#if defined(NEW_UID_IMPL)
    buffer->Save(StructureUID.GetRawData(), 16);
    buffer->Save(IncludeStructureUID.GetRawData(), 16);
    buffer->Save(ContentUID.GetRawData(), 16);
    buffer->Save(IncludeContentUID.GetRawData(), 16);
    buffer->Save(FullUID.GetRawData(), 16);
    buffer->Save(SelfUID.GetRawData(), 16);
#else
    buffer->Save(IncludedContextSign.GetRawData(), 16);
    buffer->Save(ContextSign.GetRawData(), 16);
    buffer->Save(SelfContextSign.GetRawData(), 16);
    buffer->Save(IncludedSelfContextSign.GetRawData(), 16);
    buffer->Save(RenderId.GetRawData(), 16);
#endif
}

bool TJSONEntryStats::Load(TLoadBuffer* buffer, const TDepGraph& graph) noexcept {
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

#if defined (NEW_UID_IMPL)
    buffer->LoadMd5(&StructureUID);
    buffer->LoadMd5(&IncludeStructureUID);
    buffer->LoadMd5(&ContentUID);
    buffer->LoadMd5(&IncludeContentUID);
    buffer->LoadMd5(&FullUID);
    buffer->LoadMd5(&SelfUID);

    IsSelfUIDCompleted = true;
    IsFullUIDCompleted = true;

    Finished = true;
#else
    buffer->LoadMd5(&IncludedContextSign);
    buffer->LoadMd5(&ContextSign);
    buffer->LoadMd5(&SelfContextSign);
    buffer->LoadMd5(&IncludedSelfContextSign);
    buffer->LoadMd5(&RenderId);
#endif

    HasUsualEntry = false;
    WasVisited = false;
    WasFresh = false;

    Completed = true;

    return true;
}
