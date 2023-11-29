#pragma once

#include "stats_enums.h"

#include <devtools/ymake/diag/stats_enums.h_serialized.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/cast.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

namespace NStats {
    class TStatsBase {
    public:
        void Report() const;

    protected:
        using TDataType = TVector<ui64>;

        TStatsBase(size_t size, const TString& name) : Data(size, 0), Name(name) {
            Y_ASSERT(size > 0);
        }
        TStatsBase(const TStatsBase&) = default;
        TStatsBase(TStatsBase&&) = default;
        TStatsBase& operator=(const TStatsBase&) = default;
        TStatsBase& operator=(TStatsBase&&) = default;
        virtual ~TStatsBase() = default;

        ui64 Get(size_t index) const;
        void Set(size_t index, ui64 value);
        void SetMin(size_t index, ui64 value);
        void SetMax(size_t index, ui64 value);
        void Inc(size_t index, ui64 value = 1);
        size_t Size() const;
        virtual TString NameByIndex(size_t index) const = 0;

    private:
        TDataType Data;
        TString Name;
    };

    template<class TIndex>
    class TStats: public TStatsBase {
    public:
        TStats(const TString& name = "Stats")
            : TStatsBase(GetEnumItemsCount<TIndex>(), name)
        {
            static_assert(std::is_enum_v<TIndex>);
            static_assert(GetEnumItemsCount<TIndex>() > 0);
        }

        ui64 Get(TIndex index) const {
            return TStatsBase::Get(static_cast<size_t>(index));
        }

        void Set(TIndex index, ui64 value) {
            TStatsBase::Set(static_cast<size_t>(index), value);
        }

        void SetMin(TIndex index, ui64 value) {
            TStatsBase::SetMin(static_cast<size_t>(index), value);
        }

        void SetMax(TIndex index, ui64 value) {
            TStatsBase::SetMax(static_cast<size_t>(index), value);
        }

        void Inc(TIndex index, ui64 value = 1) {
            TStatsBase::Inc(static_cast<size_t>(index), value);
        }

        void Clear() {
            for (size_t i = 0; i < Size(); ++i) {
                TStatsBase::Set(i, 0);
            }
        }

    protected:
        TString NameByIndex(size_t index) const override {
            return ToString(static_cast<TIndex>(index));
        }
    };

    using TModulesStats = TStats<EModulesStats>;
    using TMakeCommandStats = TStats<EMakeCommandStats>;
    using TUpdIterStats = TStats<EUpdIterStats>;
    using TResolveStats = TStats<EResolveStats>;
    using TGeneralParserStats = TStats<EGeneralParserStats>;
    using TIncParserManagerStats = TStats<EIncParserManagerStats>;
    using TFileConfStats = TStats<EFileConfStats>;
    using TFileConfSubStats = TStats<EFileConfSubStats>;
    using TDepGraphStats = TStats<EDepGraphStats>;
    using TInternalCacheSaverStats = TStats<EInternalCacheSaverStats>;
    using TJsonCacheStats = TStats<EJsonCacheStats>;
    using TUidsCacheStats = TStats<EUidsCacheStats>;
}
