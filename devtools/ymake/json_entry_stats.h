#pragma once

#include <devtools/ymake/node_debug.h>
#include <devtools/ymake/md5.h>
#include <devtools/ymake/json_md5.h>

#include <devtools/ymake/compact_graph/iter.h>

#ifdef NEW_UID_IMPL
#include "json_uid_structures.h"
#endif

#ifndef NEW_UID_IMPL
#include "json_uid_structures_empty.h"
#endif

class TModule;

class TSaveBuffer;
class TLoadBuffer;

struct TJsonStackData {
    TAutoPtr<TJsonMd5> Hash; // Md5 constructors are heavy
    const TModule* Module = nullptr;
};

using TJsonStateItem = TGraphIteratorStateItem<TJsonStackData, true>;

struct TJSONEntryStats: public TJSONEntryStatsNewUID {
    union {
        ui8 AllFlags;
        struct {  // 8 bits used
            bool IncludesMd5Started : 1;
            bool HasUsualEntry : 1;
            bool AddToOutputs : 1;
            bool WasFresh : 1;
            bool Fake : 1;

            // Из-за использования кэша, присутствие TJSONEntryStats в Nodes
            // больше не означает, что соответствующий узел был посещён.
            // Теперь факт посещения обозначен этим флагом.
            bool WasVisited : 1;

            // Признак того, что все персистентные поля были вычислены
            // или загружены из кэша;
            bool Completed : 1;

            // a (temporary) means of supporting different graph representations
            // of variable context for old and new command styles
            bool StructCmdDetected : 1;
        };
    };

    TNodeId LoopId = 0;
    TNodeId OutTogetherDependency = 0;

    THolder<TUniqVector<TNodeId>> IncludedDeps;
    THolder<TUniqVector<TNodeId>> NodeDeps;
    THolder<TUniqVector<TNodeId>> NodeToolDeps;
    THolder<TUniqVector<TNodeId>> ExtraOuts;

    THolder<THashSet<TString>> UsedReservedVars;
    bool IsGlobalVarsCollectorStarted;

public:
    TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack = false, bool isFile = false);

    TString GetNodeUid() const;
    TString GetNodeSelfUid() const;

    void SetIncludedContextSign(const TMd5SigValue& md5);
    void SetIncludedContextSign(const TMd5Value& oldMd5);

    void SetContextSign(const TMd5SigValue& md5, TUidDebugNodeId id);
    void SetContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt);

    void SetSelfContextSign(const TMd5SigValue& md5, TUidDebugNodeId id);
    void SetSelfContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt);

    void SetIncludedSelfContextSign(const TMd5Value& oldMd5) {
        IncludedSelfContextSign.CopyFrom(oldMd5);
    }

    void SetIncludedSelfContextSign(const TMd5SigValue& md5) {
        IncludedSelfContextSign = md5;
    }

    const TMd5SigValue& GetIncludedContextSign() const {
        return IncludedContextSign;
    }

    void SetRenderId(const TMd5SigValue& md5, TUidDebugNodeId) {
        ContextSign = md5;
    }

    void SetRenderId(const TMd5Value& oldMd5, TUidDebugNodeId) {
        RenderId.CopyFrom(oldMd5);
    }

    const TMd5SigValue& GetRenderId() const {
        return RenderId;
    }

    const TMd5SigValue& GetContextSign() const {
        return ContextSign;
    }

    const TMd5SigValue& GetSelfContextSign() const {
        return SelfContextSign;
    }

    const TMd5SigValue& GetIncludedSelfContextSign() const {
        return IncludedSelfContextSign;
    }

    using TItemDebug = TNodeDebugOnly;

    void Save(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept;
    bool Load(TLoadBuffer* buffer, const TDepGraph& graph) noexcept;

private:
    TMd5SigValue IncludedContextSign;
    TMd5SigValue ContextSign;
    TMd5SigValue SelfContextSign; // Without Deps content
    TMd5SigValue IncludedSelfContextSign;
    TMd5SigValue RenderId; // First part of PartialMatchKey in JSON-caching
};

template <>
struct TVisitorFreshTraits<TJSONEntryStats> {
    static bool IsFresh(const TJSONEntryStats& item, bool wasJustCreated) {
        if (wasJustCreated)
            return true;
        return !item.WasVisited;
    }
};

struct TLoopCnt {
    TLoopCnt()
        : Sign("TLoopCnt::Sign"sv)
        , SelfSign("TLoopCnt::SelfSign"sv)
    {
    }

    TMd5SigValue Sign;
    TMd5SigValue SelfSign;
};
