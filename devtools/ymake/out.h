#pragma once

#include <devtools/ymake/diag/dbg.h>

#include <util/stream/str.h>
#include <util/stream/output.h>
#include <util/generic/vector.h>
#include <util/generic/string.h>

template <typename T>
struct TStringRepr {
    static TString ToString(const T& t) {
        TString res;
        TStringOutput outs(res);
        outs << t;
        return res;
    }
};

template <typename T>
struct TStringRepr<T*> {
    static TString ToString(T* t) {
        TString res;
        TStringOutput outs(res);
        outs << "-> " << *t;
        return res;
    }
};

struct TVecDump {
    const TVector<TString>& V;
    TVecDump(const TVector<TString>& v)
        : V(v)
    {
    }
};

struct TVecDumpSb {
    const TVector<TStringBuf>& V;
    TVecDumpSb(const TVector<TStringBuf>& v)
        : V(v)
    {
    }
};

template <typename TSeq>
struct TSeqDump {
    const TSeq& V;
    TSeqDump(const TSeq& v)
        : V(v)
    {
    }
};

template <typename TSeq>
TSeqDump<TSeq> SeqDump(const TSeq& seq) {
    return TSeqDump<TSeq>(seq);
}
