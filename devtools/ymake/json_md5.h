#pragma once

#include "md5.h"

#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/loops.h>

#ifdef NEW_UID_IMPL
#include "json_uid_structures.h"
#endif

#ifndef NEW_UID_IMPL
#include "json_uid_structures_empty.h"
#endif

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

class TJsonMd5 : public TJsonMD5NewUID {
private:
    // To calculate UID
    TMd5Value IncludesMd5;
    TMd5Value ContextMd5;
    // To calculate self UID (without Deps uid)
    TMd5Value IncludesSelfContextMd5;
    TMd5Value SelfContextMd5;
    // To be used as part of RenderId in JSON-caching
    TMd5Value RenderMd5;

    TString NodeName;
    TUidDebugNodeId NodeId;

    const TSymbols& SymbolsTable;

public:
    TJsonMd5(TNodeDebugOnly nodeDebug, const TString& name, const TSymbols& symbols);

    void IncludesMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void IncludesMd5Update(const char* data, size_t len);

    void ContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void ContextMd5Update(const char* data, size_t len);

    void SelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void SelfContextMd5Update(const char* data, size_t len);

    void IncludesSelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void IncludesSelfContextMd5Update(const char* data, size_t len);

    void RenderMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void RenderMd5Update(const char* data, size_t len);

    TUidDebugNodeId GetId() const {
        return NodeId;
    }

    TStringBuf GetName() const {
        return NodeName;
    }

    const TMd5Value& GetContextMd5() const {
        return ContextMd5;
    }

    const TMd5Value& GetIncludesMd5() const {
        return IncludesMd5;
    }

    const TMd5Value& GetRenderMd5() const {
        return RenderMd5;
    }

    const TMd5Value& GetSelfContextMd5() const {
        return SelfContextMd5;
    }

    const TMd5Value& GetIncludesSelfContextMd5() const {
        return IncludesSelfContextMd5;
    }

    void LogContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const;
    void LogIncludedChange(const TMd5Value&  IncludesMd5, TUidDebugNodeId depNodeId) const;

    void LogSelfContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const;
    void LogSelfIncludedChange(const TMd5Value&  IncludesMd5, TUidDebugNodeId depNodeId) const;

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
