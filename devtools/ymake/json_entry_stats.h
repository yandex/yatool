#pragma once

#include <devtools/ymake/node_debug.h>
#include <devtools/ymake/md5.h>
#include <devtools/ymake/json_md5.h>

#include <devtools/ymake/compact_graph/iter.h>

class TModule;

class TSaveBuffer;
class TLoadBuffer;

struct TJsonStackData {
    TAutoPtr<TJsonMd5Base> Hash; // Md5 constructors are heavy
    const TModule* Module = nullptr;
};

using TJsonStateItem = TGraphIteratorStateItem<TJsonStackData, true>;

class TJsonStatsOld;
class TJsonStatsNew;

class TJsonStatsBase {
public:
    virtual ~TJsonStatsBase() noexcept;

    inline TJsonStatsOld* Old() noexcept;
    inline const TJsonStatsOld* Old() const noexcept;

    inline TJsonStatsNew* New() noexcept;
    inline const TJsonStatsNew* New() const noexcept;
};

class TJsonStatsOld : public TJsonStatsBase {
public:
    TJsonStatsOld(TNodeDebugOnly nodeDebug);

    const TMd5SigValue& GetContextSign() const {
        return ContextSign;
    }

    const TMd5SigValue& GetIncludedContextSign() const {
        return IncludedContextSign;
    }

    const TMd5SigValue& GetSelfContextSign() const {
        return SelfContextSign;
    }

    const TMd5SigValue& GetIncludedSelfContextSign() const {
        return IncludedSelfContextSign;
    }

    const TMd5SigValue& GetRenderId() const {
        return RenderId;
    }

    TString GetNodeUid() const {
        return ContextSign.ToBase64();
    }

    TString GetNodeSelfUid() const {
        return SelfContextSign.ToBase64();
    }

    void SetContextSign(const TMd5SigValue& md5, TUidDebugNodeId id);
    void SetContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt);

    void SetIncludedContextSign(const TMd5SigValue& md5);
    void SetIncludedContextSign(const TMd5Value& oldMd5);

    void SetSelfContextSign(const TMd5SigValue& md5, TUidDebugNodeId id);
    void SetSelfContextSign(const TMd5Value& oldMd5, TUidDebugNodeId id, TStringBuf contextSalt);

    void SetIncludedSelfContextSign(const TMd5Value& oldMd5) {
        IncludedSelfContextSign.CopyFrom(oldMd5);
    }

    void SetIncludedSelfContextSign(const TMd5SigValue& md5) {
        IncludedSelfContextSign = md5;
    }

    void SetRenderId(const TMd5SigValue& md5, TUidDebugNodeId) {
        ContextSign = md5;
    }

    void SetRenderId(const TMd5Value& oldMd5, TUidDebugNodeId) {
        RenderId.CopyFrom(oldMd5);
    }

    TMd5SigValue ContextSign;
    TMd5SigValue IncludedContextSign;
    TMd5SigValue SelfContextSign; // Without Deps content
    TMd5SigValue IncludedSelfContextSign;
    TMd5SigValue RenderId; // First part of PartialMatchKey in JSON-caching
};

class TJsonStatsNew : public TJsonStatsBase {
public:
    TJsonStatsNew(TNodeDebugOnly nodeDebug);

    void SetStructureUid(const TMd5SigValue& md5);
    void SetStructureUid(const TMd5Value& oldMd5);

    void SetIncludeStructureUid(const TMd5SigValue& md5);
    void SetIncludeStructureUid(const TMd5Value& oldMd5);

    void SetContentUid(const TMd5SigValue& md5);
    void SetContentUid(const TMd5Value& oldMd5);

    void SetIncludeContentUid(const TMd5SigValue& md5);
    void SetIncludeContentUid(const TMd5Value& oldMd5);

    void SetFullUid(const TMd5Value& oldMd5);
    void SetFullUid(const TMd5SigValue& oldMd5);
    void SetSelfUid(const TMd5Value& oldMd5);

    TString GetNodeUid() const {
        return GetFullUid().ToBase64();
    }

    TString GetNodeSelfUid() const {
        return GetSelfUid().ToBase64();
    }

    const TMd5SigValue& GetStructureUid() const {
        Y_ASSERT(Finished);
        return StructureUID;
    }

    const TMd5SigValue& GetPreStructureUid() const {
        Y_ASSERT(Finished);
        return PreStructureUID;
    }

    const TMd5SigValue& GetIncludeStructureUid() const {
        Y_ASSERT(Finished);
        return IncludeStructureUID;
    }

    const TMd5SigValue& GetContentUid() const {
        Y_ASSERT(Finished);
        return ContentUID;
    }

    const TMd5SigValue& GetIncludeContentUid() const {
        Y_ASSERT(Finished);
        return IncludeContentUID;
    }

    const TMd5SigValue& GetFullUid() const {
        return FullUID;
    }

    const TMd5SigValue& GetSelfUid() const {
        return SelfUID;
    }

    TString GetFullNodeUid() const {
        return FullUID.ToBase64();
    }

    TString GetSelfNodeUid() const {
        return SelfUID.ToBase64();
    }

    bool IsFullUidCompleted() const {
        return IsFullUIDCompleted;
    }

    bool IsSelfUidCompleted() const {
        return IsSelfUIDCompleted;
    }

    size_t EnterDepth = 0;
    bool Finished = false;
    bool Stored = false;

    TMd5SigValue StructureUID;
    TMd5SigValue PreStructureUID;
    TMd5SigValue IncludeStructureUID;
    TMd5SigValue ContentUID;
    TMd5SigValue IncludeContentUID;

    TMd5SigValue FullUID;
    TMd5SigValue SelfUID;

    bool IsFullUIDCompleted;
    bool IsSelfUIDCompleted;
};

inline TJsonStatsOld* TJsonStatsBase::Old() noexcept {
    Y_ASSERT(dynamic_cast<TJsonStatsOld*>(this) != nullptr);
    return static_cast<TJsonStatsOld*>(this);
}

inline const TJsonStatsOld* TJsonStatsBase::Old() const noexcept {
    Y_ASSERT(dynamic_cast<const TJsonStatsOld*>(this) != nullptr);
    return static_cast<const TJsonStatsOld*>(this);
}

inline TJsonStatsNew* TJsonStatsBase::New() noexcept {
    Y_ASSERT(dynamic_cast<TJsonStatsNew*>(this) != nullptr);
    return static_cast<TJsonStatsNew*>(this);
}

inline const TJsonStatsNew* TJsonStatsBase::New() const noexcept {
    Y_ASSERT(dynamic_cast<const TJsonStatsNew*>(this) != nullptr);
    return static_cast<const TJsonStatsNew*>(this);
}

struct TJSONEntryStatsNewUID : public TEntryStats, public TNodeDebugOnly {
    TJSONEntryStatsNewUID(TNodeDebugOnly nodeDebug, bool inStack, bool isFile);


protected:
};

struct TJsonDepsDebug : public TNodeDebug {
    const char* DebugName;

    TJsonDepsDebug(const TNodeDebug& nodeDebug, const char* debugName)
        : TNodeDebug(nodeDebug)
        , DebugName(debugName)
    {
    }
};

using TJsonDepsDebugOnly = TDebugOnly<TJsonDepsDebug>;

struct TJsonDeps :
    public THolder<TUniqVector<TNodeId>>,
    public TJsonDepsDebugOnly
{
    using TJsonDepsDebugOnly::TJsonDepsDebugOnly;

    void Add(TNodeId id) {
        AddTo(id, *this);

        if constexpr (DebugEnabled) {
            TraceAdd(id);
        }
    }

    void Add(const TJsonDeps& ids) {
        AddTo<TUniqVector<TNodeId>>(ids, *this);

        if constexpr (DebugEnabled) {
            if (ids) {
                for (TNodeId id : *ids) {
                    TraceAdd(id);
                }
            }
        }
    }

private:
    void TraceAdd(TNodeId id);
};

struct TJSONEntryStats : public TEntryStats, public TNodeDebugOnly  {
    union {
        ui8 AllFlags;
        struct {  // 7 bits used
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
        };
    };

    TNodeId LoopId = 0;
    TNodeId OutTogetherDependency = 0;

    TJsonDeps IncludedDeps;
    TJsonDeps NodeDeps;
    TJsonDeps NodeToolDeps;
    THolder<TUniqVector<TNodeId>> ExtraOuts;
    THolder<TJsonStatsBase> Uids;

    THolder<THashSet<TString>> UsedReservedVars;
    bool IsGlobalVarsCollectorStarted;

public:
    TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack = false, bool isFile = false);

    void InitUids(bool newUids) {
        if (Uids)
            return;
        if (newUids)
            Uids.Reset(new TJsonStatsNew{*this});
        else
            Uids.Reset(new TJsonStatsOld{*this});
    }

    TString GetNodeUid(bool newUids) const;
    TString GetNodeSelfUid(bool newUids) const;

    TJsonStatsOld* OldUids() noexcept {
        return Uids.Get()->Old();
    }

    const TJsonStatsOld* OldUids() const noexcept {
        return Uids.Get()->Old();
    }

    TJsonStatsNew* NewUids() noexcept {
        return Uids.Get()->New();
    }

    const TJsonStatsNew* NewUids() const noexcept {
        return Uids.Get()->New();
    }

    using TItemDebug = TNodeDebugOnly;

    void Save(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept;
    void LoadStructureUid(TLoadBuffer* buffer, const TDepGraph& graph, bool asPre = false) noexcept;
    bool Load(TLoadBuffer* buffer, const TDepGraph& graph) noexcept;

private:
    void SaveStructureUid(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept;
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
