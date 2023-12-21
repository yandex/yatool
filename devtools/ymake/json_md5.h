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

class TJsonMd5Old;
class TJsonMd5New;

class TJsonMd5Base {
public:
    virtual ~TJsonMd5Base() noexcept;

    inline TJsonMd5Old* Old() noexcept;
    inline TJsonMd5New* New() noexcept;
};

class TJsonMd5Old : public TJsonMd5Base {
private:
    TString NodeName;
    TUidDebugNodeId NodeId;

    const TSymbols& SymbolsTable;

    // To calculate UID
    TMd5Value ContextMd5;
    TMd5Value IncludesMd5;
    // To calculate self UID (without Deps uid)
    TMd5Value SelfContextMd5;
    TMd5Value IncludesSelfContextMd5;
    // To be used as part of RenderId in JSON-caching
    TMd5Value RenderMd5;

public:
    TJsonMd5Old(TNodeDebugOnly nodeDebug, const TString& name, const TSymbols& symbols);

    TStringBuf GetName() const {
        return NodeName;
    }

    TUidDebugNodeId GetId() const {
        return NodeId;
    }

    void ContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void ContextMd5Update(const char* data, size_t len);

    void IncludesMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void IncludesMd5Update(const char* data, size_t len);

    void SelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void SelfContextMd5Update(const char* data, size_t len);

    void IncludesSelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void IncludesSelfContextMd5Update(const char* data, size_t len);

    void RenderMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName);
    void RenderMd5Update(const char* data, size_t len);

    const TMd5Value& GetContextMd5() const {
        return ContextMd5;
    }

    const TMd5Value& GetIncludesMd5() const {
        return IncludesMd5;
    }

    const TMd5Value& GetSelfContextMd5() const {
        return SelfContextMd5;
    }

    const TMd5Value& GetIncludesSelfContextMd5() const {
        return IncludesSelfContextMd5;
    }

    const TMd5Value& GetRenderMd5() const {
        return RenderMd5;
    }

    void LogContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const;
    void LogIncludedChange(const TMd5Value&  IncludesMd5, TUidDebugNodeId depNodeId) const;

    void LogSelfContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const;
    void LogSelfIncludedChange(const TMd5Value&  IncludesMd5, TUidDebugNodeId depNodeId) const;
};

class TJsonMd5New : public TJsonMd5Base {
private:
    TMd5Value StructureMd5;
    TMd5Value IncludeStructureMd5;
    TMd5Value ContentMd5;
    TMd5Value IncludeContentMd5;

public:
    TJsonMd5New(TNodeDebugOnly nodeDebug);

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

inline TJsonMd5Old* TJsonMd5Base::Old() noexcept {
    Y_ASSERT(dynamic_cast<TJsonMd5Old*>(this) != nullptr);
    return static_cast<TJsonMd5Old*>(this);
}

inline TJsonMd5New* TJsonMd5Base::New() noexcept {
    Y_ASSERT(dynamic_cast<TJsonMd5Old*>(this) != nullptr);
    return static_cast<TJsonMd5New*>(this);
}

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
