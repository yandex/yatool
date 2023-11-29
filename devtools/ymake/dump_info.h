#pragma once

#include "vars.h"

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/graph.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

struct TDumpInfoEx {
    TYVar ExtraInput;
    TYVar ExtraOutput;
    TVector<TString> LateOuts;
    TVector<TString> Inputs;
    TUniqVector<TNodeId> Deps;
    TUniqVector<TNodeId> ToolDeps;
    virtual void SetExtraValues(TVars&) = 0;
    virtual ~TDumpInfoEx() {
    }
};

struct TDumpInfoUID : TDumpInfoEx {
    const TString UID;
    const TString SelfUID;

    TDumpInfoUID(const TString& uid, const TString& selfUid)
        : UID(uid), SelfUID(selfUid)
    {
    }

    void SetExtraValues(TVars& vars) override {
        vars.SetValue("UID", UID);
    }
};
