#pragma once

#include <devtools/ymake/symbols/elem_id.h>
#include <devtools/ymake/libs/clocks/checkpoint.h>

#include <util/generic/hash.h>

#include <chrono>
#include <source_location>
#include <string_view>

namespace NDetail {

struct TModuleExtremum {
    TFileElemId Mod = {};
    std::chrono::nanoseconds Value = {};
};

struct TModStageStats {
    size_t Count = 0;
    std::chrono::nanoseconds Total = {};
    TModuleExtremum Min = {{}, std::chrono::nanoseconds::max()};
    TModuleExtremum Max = {{}, std::chrono::nanoseconds::min()};
};

class TScopedMeasurer {
public:
    explicit TScopedMeasurer(TModStageStats& dest, TFileElemId mod) noexcept;
    ~TScopedMeasurer() noexcept;
private:
    TCheckPoint<std::chrono::steady_clock> Checkpoint_;
    TFileElemId Mod_;
    TModStageStats& Dest_;
};

}

class TNameStore;

class TModuleStagesStatsManager {
public:
    [[nodiscard]]
    NDetail::TScopedMeasurer Measure(
        TFileElemId mod = FindCurrentModule(),
        std::source_location loc = std::source_location::current()
    );

    void Report(const TNameStore& names);
    void CheckUnreported() const;

    static TModuleStagesStatsManager& Current();

private:
    static TFileElemId FindCurrentModule();

private:
    THashMap<std::string_view, NDetail::TModStageStats> Stages_;
};
