#pragma once

#include "md5.h"

#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/loops.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/vector.h>
#include <util/string/builder.h>

class TSymbols;

enum class TUidDebugNodeId : size_t {Invalid = 0};

template<>
inline void Out<TUidDebugNodeId>(IOutputStream& os, TTypeTraits<TUidDebugNodeId>::TFuncParam i){
    os << static_cast<size_t>(i);
}

namespace NUidDebug {
    bool Enabled();
    TUidDebugNodeId GetNodeId(TStringBuf name, const TSymbols& names);
    TUidDebugNodeId LogNodeDeclaration(TStringBuf name); // returns node id
    void LogNodeDeclaration(TStringBuf name, TUidDebugNodeId id);
    void LogDependency(TUidDebugNodeId from, TUidDebugNodeId to);
    void LogSelfDependency(TUidDebugNodeId from, TUidDebugNodeId to);
    void LogContextMd5Assign(TUidDebugNodeId nodeId, const TString& value);
    void LogIncludedMd5Assign(TUidDebugNodeId nodeId, const TString& value);
    void LogSelfContextMd5Assign(TUidDebugNodeId nodeId, const TString& value);
    void LogSelfIncludedMd5Assign(TUidDebugNodeId nodeId, const TString& value);

    TString LoopNodeName(TNodeId loopId);
};

class TJsonMd5 {
private:
    TMd5Value StructureMd5;
    TMd5Value IncludeStructureMd5;
    TMd5Value ContentMd5;
    TMd5Value IncludeContentMd5;

public:
    TJsonMd5(TNodeDebugOnly nodeDebug);

    void StructureMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        StructureMd5.Update(value, reason);
    }

    void IncludeStructureMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        IncludeStructureMd5.Update(value, reason);
    }

    void ContentMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        ContentMd5.Update(value, reason);
    }

    void IncludeContentMd5Update(const TMd5UpdateValue& value, TStringBuf reason) {
        IncludeContentMd5.Update(value, reason);
    }

    const TMd5Value& GetStructureMd5() const {
        return StructureMd5;
    }

    const TMd5Value& GetIncludeStructureMd5() const {
        return IncludeStructureMd5;
    }

    const TMd5Value& GetContentMd5() const {
        return ContentMd5;
    }

    const TMd5Value& GetIncludeContentMd5() const {
        return IncludeContentMd5;
    }
};

class TJsonMultiMd5 {
private:
    TUidDebugNodeId NodeId;
    const TSymbols& SymbolsTable;

    struct TSignData {
        bool IsLoopPart;
        TMd5SigValue Md5;
        TString Name;

        bool operator<(const TSignData& other) const {
            return Md5 < other.Md5;
        }
    };

    TVector<TSignData> Signs;

public:
    explicit TJsonMultiMd5(TNodeId loopId, const TSymbols& symbols, size_t expectedSize);

    void AddSign(const TMd5SigValue& sign, TStringBuf depName, bool isLoopPart);
    void CalcFinalSign(TMd5SigValue& res);

    size_t Size() const {
        return Signs.size();
    }

    TUidDebugNodeId GetLoopNodeId() const {
        return NodeId;
    }
};
