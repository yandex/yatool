#include "json_entry_stats.h"

#include "json_saveload.h"

namespace {

    TString FormatURV(const TUsedReservedVars::TSet* x) {
        if (!x)
            return "<null>";
        TStringBuilder result;
        result << "[";
        for (auto& y : *x) {
            result << (&y == &*x->begin() ? "" : ", ") << y;
        }
        result << "]";
        return result;
    }

    TString FormatURV(const TUsedReservedVars::TMap* x) {
        if (!x)
            return "<null>";
        TStringBuilder result;
        result << "[";
        for (auto& y : *x) {
            result << (&y == &*x->begin() ? "" : ", ") << y.first << " = " << FormatURV(&y.second);
        }
        result << "]";
        return result;
    }

}

void TJSONEntryStats::SetStructureUid(const TMd5SigValue& md5) {
    StructureUID = md5;
    YDIAG(Dev) << "Set StructureUID, value is " << StructureUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetStructureUid(const TMd5Value& oldMd5) {
    StructureUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set StructureUID, value is " << StructureUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetIncludeStructureUid(const TMd5SigValue& md5) {
    IncludeStructureUID = md5;
    YDIAG(Dev) << "Set IncludeStructureUID, value is " << IncludeStructureUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetIncludeStructureUid(const TMd5Value& oldMd5) {
    IncludeStructureUID.CopyFrom(oldMd5);
}

void TJSONEntryStats::SetContentUid(const TMd5SigValue& md5) {
    ContentUID = md5;
    YDIAG(Dev) << "Set ContentUID, value is " << ContentUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetContentUid(const TMd5Value& oldMd5) {
    ContentUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set ContentUID, value is " << ContentUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetIncludeContentUid(const TMd5SigValue& md5) {
    IncludeContentUID = md5;
    YDIAG(Dev) << "Set IncludeContentUID, value is " << IncludeContentUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetIncludeContentUid(const TMd5Value& oldMd5) {
    IncludeContentUID.CopyFrom(oldMd5);
    YDIAG(Dev) << "Set ContentUID, value is " << IncludeContentUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetFullUid(const TMd5Value& oldMd5) {
    FullUID.CopyFrom(oldMd5);
    IsFullUIDCompleted = true;
    YDIAG(Dev) << "Set FullUID, value is " << FullUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetFullUid(const TMd5SigValue& oldMd5) {
    FullUID = oldMd5;
    IsFullUIDCompleted = true;
    YDIAG(Dev) << "Set FullUID, value is " << FullUID.ToBase64() << Endl;
}

void TJSONEntryStats::SetSelfUid(const TMd5Value& oldMd5) {
    SelfUID.CopyFrom(oldMd5);
    IsSelfUIDCompleted = true;
    YDIAG(Dev) << "Set SelfUID, value is " << SelfUID.ToBase64() << Endl;
}

void TJsonDeps::TraceAdd(TNodeId id) {
    Y_UNUSED(id);
}

TJSONEntryStats::TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack, bool isFile)
    : TEntryStats(nodeDebug, inStack, isFile)
    , TNodeDebugOnly{nodeDebug}
    , AllFlags(0)
    , IncludedDeps(nodeDebug, "IncludedDeps")
    , NodeDeps(nodeDebug, "NodeDeps")
    , NodeToolDeps(nodeDebug, "NodeToolDeps")
    , IsGlobalVarsCollectorStarted(false)
    , StructureUID(nodeDebug, "TJSONEntryStats::StructureUID"sv)
    , PreStructureUID(nodeDebug, "TJSONEntryStats::PreStructureUID"sv)
    , IncludeStructureUID(nodeDebug, "TJSONEntryStats::IncludeStructureUID"sv)
    , ContentUID(nodeDebug, "TJSONEntryStats::ContentUID"sv)
    , IncludeContentUID(nodeDebug, "TJSONEntryStats::IncludeContentUID"sv)
    , FullUID(nodeDebug, "TJSONEntryStats::FullUID"sv)
    , SelfUID(nodeDebug, "TJSONEntryStats::SelfUID"sv)
    , IsFullUIDCompleted(false)
    , IsSelfUIDCompleted(false)
{
}

void TJSONEntryStats::SaveStructureUid(TSaveBuffer* buffer, const TDepGraph&) const noexcept {
    buffer->Save(StructureUID.GetRawSig());
}

void TJSONEntryStats::Save(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept {
    SaveStructureUid(buffer, graph);
    buffer->Save<ui8>(static_cast<const TEntryStatsData*>(this)->AllFlags);
    buffer->Save<ui8>(AllFlags);
    buffer->SaveElemId(OutTogetherDependency, graph);
    buffer->SaveElemIds(IncludedDeps, graph);
    buffer->SaveElemIds(NodeDeps, graph);
    buffer->SaveElemIds(NodeToolDeps, graph);
    buffer->SaveElemIds(ExtraOuts, graph);
    buffer->SaveReservedVars(UsedReservedVarsLocal.FromCmd.Get(), graph);
    buffer->SaveReservedVarsTotals(UsedReservedVarsLocal.FromVars.Get(), graph);
    buffer->Save(IncludeStructureUID.GetRawSig());
    buffer->Save(ContentUID.GetRawSig());
    buffer->Save(IncludeContentUID.GetRawSig());
    buffer->Save(FullUID.GetRawSig());
    buffer->Save(SelfUID.GetRawSig());

    YDIAG(UIDs) << "Saving URVTT " << FormatURV(UsedReservedVarsLocal.FromCmd.Get()) << Endl;
    YDIAG(UIDs) << "Saving URVLC " << FormatURV(UsedReservedVarsLocal.FromCmd.Get()) << Endl;
    YDIAG(UIDs) << "Saving URVLV " << FormatURV(UsedReservedVarsLocal.FromVars.Get()) << Endl;
}

void TJSONEntryStats::LoadStructureUid(TLoadBuffer* buffer, bool asPre) noexcept {
    if (asPre) {
        buffer->LoadMd5(&PreStructureUID);
    } else {
        buffer->LoadMd5(&StructureUID);
        PreStructureUID.SetRawData(StructureUID.GetRawData(), "Copy StructureUid"sv);
    }
}

bool TJSONEntryStats::Load(TLoadBuffer* buffer, const TDepGraph& graph) noexcept {
    LoadStructureUid(buffer);
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
    if (!buffer->LoadReservedVars(&UsedReservedVarsLocal.FromCmd, graph))
        return false;
    if (!buffer->LoadReservedVarsTotals(&UsedReservedVarsLocal.FromVars, graph))
        return false;

    YDIAG(UIDs) << "Loaded URVTT " << FormatURV(UsedReservedVarsTotal.Get()) << Endl;
    YDIAG(UIDs) << "Loaded URVLC " << FormatURV(UsedReservedVarsLocal.FromCmd.Get()) << Endl;
    YDIAG(UIDs) << "Loaded URVLV " << FormatURV(UsedReservedVarsLocal.FromVars.Get()) << Endl;

    buffer->LoadMd5(&IncludeStructureUID);
    buffer->LoadMd5(&ContentUID);
    buffer->LoadMd5(&IncludeContentUID);
    buffer->LoadMd5(&FullUID);
    buffer->LoadMd5(&SelfUID);

    IsSelfUIDCompleted = true;
    IsFullUIDCompleted = true;

    Finished = true;

    HasUsualEntry = false;
    WasVisited = false;
    WasFresh = false;

    Completed = true;

    return true;
}
