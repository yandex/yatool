#include "stats.h"
#include "trace.h"

#include <library/cpp/iterator/mapped.h>

#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/generic/strbuf.h>
#include <util/generic/utility.h>
#include <util/generic/xrange.h>

namespace NStats {
    void TStatsBase::Report() const {
        auto&& range = MakeMappedRange(xrange(Size()), [this](auto index) {
            return TStringBuilder{} << NameByIndex(index) << " = "sv << Get(index) << ';';
        });
        TString message = JoinSeq(TStringBuf(" "), range);
        NEvent::TDisplayMessage msg;
        msg.SetType("Debug");
        msg.SetSub(Name);
        msg.SetMod("unimp");
        msg.SetMessage(message);
        FORCE_TRACE(U, msg);
    }

    ui64 TStatsBase::Get(size_t index) const {
        return Data[index];
    }

    void TStatsBase::Set(size_t index, ui64 value) {
        Data[index] = value;
    }

    void TStatsBase::SetMin(size_t index, ui64 value) {
        Data[index] = Min(Data[index], value);
    }

    void TStatsBase::SetMax(size_t index, ui64 value) {
        Data[index] = Max(Data[index], value);
    }

    void TStatsBase::Inc(size_t index, ui64 value) {
        Data[index] += value;
    }

    size_t TStatsBase::Size() const {
        return Data.size();
    }
}
