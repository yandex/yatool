#pragma once

#include "display.h"
#include "diag.h"
#include "dbg.h"

#include <util/generic/vector.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/set.h>
#include <util/memory/blob.h>
#include <util/generic/fwd.h>
#include <util/stream/str.h>
#include <util/ysaveload.h>

class TMultiBlobBuilder;

namespace NDetail {

    class TConfigureTraceDeduplicator {
    public:
        bool CanTrace(TFileElemId fileElemId, ETraceEvent what) {
            return Traced.insert({fileElemId, what}).second;
        }

        void Clear() {
            Traced.clear();
        }

    private:
        THashSet<std::pair<TFileElemId, ETraceEvent>> Traced;
    };

}

struct TConfigureMessage {
    EConfMsgType Type;
    TString Kind;
    TStringStream Message;
    size_t Row;
    size_t Column;
    bool IsPersistent = true;

    TConfigureMessage(EConfMsgType type, TStringBuf kind, TStringStream message, size_t row = 0, size_t column = 0, bool isPersistent = true)
        : Type(type)
        , Kind(kind)
        , Message(message)
        , Row(row)
        , Column(column)
        , IsPersistent(isPersistent)
    {
    }

    TConfigureMessage() = default;

    Y_SAVELOAD_DEFINE(Type, Kind, Message.Str(), Row, Column);
};

struct TConfigureEvent {
    ETraceEvent Type;
    TString Event;
    bool IsPersistent = true;

    TConfigureEvent(ETraceEvent what, const TString& event, bool isPersistent = true)
        : Type(what)
        , Event(event)
        , IsPersistent(isPersistent)
    {
    }

    TConfigureEvent() = default;

    Y_SAVELOAD_DEFINE(Type, Event);
};

class TConfMsgManager {
public:
    using TStreamMessage = std::shared_ptr<IOutputStream>;
    template<class T>
    using TConfValuesMap = THashMap<TFileElemId, TVector<T>>;

    TConfMsgManager(bool delayed = false)
        : Delayed(delayed)
    {
    }

    void Flush() const;
    void Flush(TFileElemId owner);
    void FlushTopLevel() const;

    void ReportConfigureEvent(ETraceEvent what, const TString& event);
    TStreamMessage ReportConfigureMessage(EConfMsgType type, TStringBuf var, size_t row = 0, size_t column = 0);
    void AddDupSrcLink(TFileElemId id, TFileElemId modid, bool force = true);
    void AddVisitedModule(TFileElemId modId);
    void ReportDupSrcConfigureErrors(std::function<TStringBuf (TFileElemId)> toString);

    void DisableDelay();
    void EnableDelay();
    bool IsDelayed() const;

    void Erase(TFileElemId owner);
    bool EraseMessagesByKind(const TStringBuf var, TFileElemId owner = TFileElemId()); // If owner == 0 erase for all owners
    bool HasMessagesByKind(const TStringBuf var, TFileElemId owner) const;

    void Load(const TBlob& blob);
    void Save(TMultiBlobBuilder& builder);

    void ClearTopLevelMessages();

    mutable bool HasConfigurationErrors = false;

private:
    void SaveEvent(ETraceEvent what, const TString& event);
    TStringStream& SaveConfigureMessage(EConfMsgType type, TStringBuf var, size_t row = 0, size_t column = 0, bool useCache = true);

    void FlushEvents(TFileElemId owner) const;
    void FlushConfigureMessages(TFileElemId owner) const;

    template<typename Container>
    void Load(IInputStream* input, THashMap<TFileElemId, Container>& confValues);
    template<typename Container>
    void Save(IOutputStream* output, THashMap<TFileElemId, Container>& confValues);

    friend bool CanTraceUniqConfigureEvent(TConfMsgManager& manager, TFileElemId elemId, ETraceEvent what) {
        return manager.TracesDeduplicator.CanTrace(elemId, what);
    }

private:
    bool Delayed;
    TConfValuesMap<TConfigureMessage> Messages;
    TConfValuesMap<TConfigureEvent> Events;
    THashMap<TFileElemId, TSet<TFileElemId>> DupSrcMap;
    THashSet<TFileElemId> VisitedModules;
    NDetail::TConfigureTraceDeduplicator TracesDeduplicator;
};

TConfMsgManager* ConfMsgManager();

// Use these for user-intended messages, that are related to graph nodes
// should be persisted in graph cache. These messages require TScopedContext.
#define YConfInfo(var) Diag()->var && TEatStream() | *ConfMsgManager()->ReportConfigureMessage(EConfMsgType::Info, "-W" #var)

#define YConfErr(var) Diag()->var && TEatStream() | *ConfMsgManager()->ReportConfigureMessage(EConfMsgType::Error, "-W" #var)
#define YConfErrPrecise(var, row, column) Diag()->var && TEatStream() | *ConfMsgManager()->ReportConfigureMessage(EConfMsgType::Error, "-W" #var, row, column)

#define YConfWarn(var) Diag()->var && TEatStream() | *ConfMsgManager()->ReportConfigureMessage(EConfMsgType::Warning, "-W" #var)
#define YConfWarnPrecise(var, row, column) Diag()->var && TEatStream() | *ConfMsgManager()->ReportConfigureMessage(EConfMsgType::Warning, "-W" #var, row, column)

#define YConfErase(var) Diag()->var && ConfMsgManager()->EraseMessagesByKind("-W" #var)
#define YConfEraseByOwner(var, owner) Diag()->var && ConfMsgManager()->EraseMessagesByKind("-W" #var, owner)
#define YConfHasMessagesByOwner(var, owner) Diag()->var && ConfMsgManager()->HasMessagesByKind("-W" #var, owner)

// This macro must not be used before ConfMsgManager()->DisableDelay and only for unstable diagnostincs which can be diferent
// depending on dep cacahe state or start target order
#define FORCE_UNIQ_CONFIGURE_TRACE(fileView, W, M) \
    if (NYMake::TraceEnabled(::ETraceEvent::W) && \
        CanTraceUniqConfigureEvent(*ConfMsgManager(), static_cast<TFileView>(fileView).GetElemId(), ::ETraceEvent::W) \
    ) { \
        NYMake::Trace(M); \
    }


template<class T>
class TSavedState {
private:
    friend class TConfMsgManager;
    TFileElemId ElemId;
    T Value;

public:
    TSavedState(TFileElemId elemId, const T& value)
        : ElemId(elemId)
        , Value(value)
    {
    }

    TSavedState() = default;

    Y_SAVELOAD_DEFINE(ElemId, Value);
};

template<class T>
class TSaver {
public:
    TVector<TSavedState<T>> Values;
    Y_SAVELOAD_DEFINE(Values);
};
