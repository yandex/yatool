#pragma once

#include "devtools/ymake/compact_graph/dep_graph.h"
#include "symbols.h"
#include "util/generic/hash.h"

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>

#include <util/system/yassert.h>
#include <util/system/defaults.h>

typedef i64 TTimeLong;

class TTimeStamps {
public:
    static constexpr const ui8 Never = 255;

    explicit TTimeStamps(TSymbols& elems)
        : Elems(elems)
        , NumTimes(1)
    {
        Times[0] = 0; // reserved
    }

    void StartSession() {
        NeedNewSession = true;
    }

    void InitSession(THashMap<ui32, TNodeData>& nodeData);

    ui8 CurStamp() {
        Y_ASSERT(!NeedNewSession);
        Y_ASSERT(NumTimes > 0);
        return (ui8)NumTimes - 1;
    }

    void Save(TMultiBlobBuilder& builder);
    void Load(TBlob& store);

private:
    struct TCntForPos;
    struct TStampMoveDesc;
    struct TMoveMap;
    void CompressTimes(THashMap<ui32, TNodeData>& nodeData);

private:
    TSymbols& Elems;

    enum {
        //FillMax = 16,
        //KeepLast = 3,
        //SingleDrop = 8
        FillMax = Never - 1,
        KeepLast = 16,
        SingleDrop = 128
    };
    TTimeLong Times[256];
    ui32 NumTimes;
    bool NeedNewSession = false;
};
