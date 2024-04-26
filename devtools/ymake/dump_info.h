#pragma once

#include "vars.h"

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/graph.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

struct TDumpInfoEx {
    TYVar ExtraOutput;
    TVector<TString> LateOuts;
    TUniqVector<TNodeId> Deps;
    TUniqVector<TNodeId> ToolDeps;
    virtual void SetExtraValues(TVars&) = 0;
    virtual ~TDumpInfoEx() {
    }

    TVector<TString>& Inputs() {
        Y_ASSERT(InputsValid_);
        return Inputs_;
    }

    void MoveInputsTo(TVector<TString>& dst) {
        Y_ASSERT(InputsValid_);
        dst.clear();
        dst.swap(Inputs_);
        InputsValid_ = false;
    }

private:
    TVector<TString> Inputs_;
    bool InputsValid_ = true;
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
