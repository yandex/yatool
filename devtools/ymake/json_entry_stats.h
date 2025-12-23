#pragma once

#include <devtools/ymake/node_debug.h>
#include <devtools/ymake/md5.h>
#include <devtools/ymake/json_md5.h>

#include <devtools/ymake/compact_graph/iter.h>

class TModule;

class TSaveBuffer;
class TLoadBuffer;

struct TJsonStackData {
    TAutoPtr<TJsonMd5> Hash; // Md5 constructors are heavy
    const TModule* Module = nullptr;
};

using TJsonStateItem = TGraphIteratorStateItem<TJsonStackData, true>;

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

    TNodeId LoopId = TNodeId::Invalid;
    TNodeId OutTogetherDependency = TNodeId::Invalid;

    TJsonDeps IncludedDeps;
    TJsonDeps NodeDeps;
    TJsonDeps NodeToolDeps;
    THolder<TUniqVector<TNodeId>> ExtraOuts;

    THolder<THashSet<TString>> UsedReservedVars;
    bool IsGlobalVarsCollectorStarted;

public:
    TJSONEntryStats(TNodeDebugOnly nodeDebug, bool inStack = false, bool isFile = false);

    using TItemDebug = TNodeDebugOnly;

    void Save(TSaveBuffer* buffer, const TDepGraph& graph) const noexcept;
    void LoadStructureUid(TLoadBuffer* buffer, bool asPre = false) noexcept;
    bool Load(TLoadBuffer* buffer, const TDepGraph& graph) noexcept;

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
    size_t Generation = 0;
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
