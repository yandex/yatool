#pragma once

#include "node_debug.h"

#include <devtools/ymake/diag/debug_log.h>

#include <atomic>

class TNodeValueDebug {
private:
    TMaybe<TNodeDebug> Node_;
    TStringBuf Name_;
    size_t Index_;

    static std::atomic<size_t> GlobalIndex_;

    TNodeValueDebug() : Index_(0) {}

public:
    friend struct TNodeValueLogEntry;

    TNodeValueDebug(const TNodeDebug& nodeDebug, TStringBuf name)
        : Node_(nodeDebug)
        , Name_(name)
        , Index_(GlobalIndex_++)
    {
    }

    TNodeValueDebug(TStringBuf name)
        : Name_(name)
        , Index_(GlobalIndex_++)
    {
    }

    TNodeValueDebug(const TNodeValueDebug& other)
        : Node_(other.Node_)
        , Name_("TNodeValueDebug::<copy-ctor>"sv)
        , Index_(GlobalIndex_++)
    {
    }

    TNodeValueDebug(const TNodeValueDebug& other, TStringBuf name)
        : Node_(other.Node_)
        , Name_(name)
        , Index_(GlobalIndex_++)
    {
    }

    TNodeValueDebug& operator=(const TNodeValueDebug&) {
        // We may copy associated value from another object,
        // but that doesn't change the identity of this object by default.
        return *this;
    }

    static const TNodeValueDebug None;
};

using TNodeValueDebugOnly = TDebugOnly<TNodeValueDebug>;

struct TNodeValueLogEntry {
    TNodeIdLog Node;
    TStringLogEntry Name;
    size_t Index = 0;

    TNodeValueLogEntry() = default;

    TNodeValueLogEntry(const TNodeValueDebug& valueId)
        : Name(TString{valueId.Name_})
        , Index(valueId.Index_)
    {
        if (valueId.Node_.Defined()) {
            auto nodeId = valueId.Node_.GetRef();
            Node = TNodeIdLog{nodeId.DebugGraph, nodeId.DebugNode};
        }
    }

    Y_SAVELOAD_DEFINE(Node, Name, Index);
};

namespace NDebugEvents {
    struct TMd5Change {
        TNodeValueLogEntry Destination;
        TNodeValueLogEntry Source;
        TStringLogEntry Reason;
        ui8 Md5[16];

        TMd5Change() = default;
        TMd5Change(const TNodeValueDebug& destination, const TNodeValueDebug& source, TStringBuf reason, const ui8* md5);

        Y_SAVELOAD_DEFINE(Destination, Source, Reason, Md5);
    };
}

class TMd5Value;
class TMd5SigValue;

void LogMd5ChangeImpl(const TMd5Value& value, const TNodeValueDebugOnly* source, TStringBuf reason);
void LogMd5ChangeImpl(const TMd5SigValue& value, const TNodeValueDebugOnly* source, TStringBuf reason);

inline void LogMd5Change(const TMd5Value& value, const TNodeValueDebugOnly* source, TStringBuf reason) {
    IF_BINARY_LOG(UIDs) {
        LogMd5ChangeImpl(value, source, reason);
    }
}

inline void LogMd5Change(const TMd5SigValue& value, const TNodeValueDebugOnly* source, TStringBuf reason) {
    IF_BINARY_LOG(UIDs) {
        LogMd5ChangeImpl(value, source, reason);
    }
}
