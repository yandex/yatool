#include "mod_stats_manager.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/trace.ev.pb.h>

#include <devtools/ymake/libs/name_store/name_store.h>

#include <devtools/ymake/context_executor.h>

namespace {

std::string_view ModName(const TNameStore& names, TFileElemId mod) {
    if (!mod)
        return TDiagCtrl::TWhere::TOP_LEVEL;
    return names.GetStringBufName(RawElemId(mod));
}

}

namespace NDetail {

TScopedMeasurer::TScopedMeasurer(TModStageStats& dest, TFileElemId mod) noexcept
    : Checkpoint_{MakeCheckpoint<std::chrono::steady_clock>()}
    , CpuCheckpoint_{MakeCheckpoint<TThreadCPUClock>()}
    , Mod_{mod}
    , Dest_{dest}
{}

TScopedMeasurer::~TScopedMeasurer() noexcept {
    const auto time = TimeSince(Checkpoint_);
    const auto cpuTime = TimeSince(CpuCheckpoint_);
    ++Dest_.Count;
    Dest_.Total += time;
    Dest_.TotalCpu += cpuTime;
    if (time < Dest_.Min.Value) {
        Dest_.Min.Mod = Mod_;
        Dest_.Min.Value = time;
        Dest_.Min.CpuValue = cpuTime;
    }
    if (time > Dest_.Max.Value) {
        Dest_.Max.Mod = Mod_;
        Dest_.Max.Value = time;
        Dest_.Max.CpuValue = cpuTime;
    }
}

}

NDetail::TScopedMeasurer TModuleStagesStatsManager::Measure(TFileElemId mod, std::source_location loc) {
    return NDetail::TScopedMeasurer{Stages_[loc.function_name()], mod};
}

void TModuleStagesStatsManager::Report(const TNameStore& names) {
    for (const auto& [stage, stats]: std::exchange(Stages_, {})) {
        NEvent::TModStageStats msg;
        msg.SetName(TString{stage});
        msg.SetCount(stats.Count);

        auto& total = *msg.MutableTotal();
        total.SetWallUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Total).count());
        total.SetCpuUs(stats.TotalCpu.count());

        auto& min = *msg.MutableMin();
        min.SetWallUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Min.Value).count());
        min.SetCpuUs(stats.Min.CpuValue.count());
        min.SetModule(TString{ModName(names, stats.Min.Mod)});

        auto& max = *msg.MutableMax();
        max.SetWallUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Max.Value).count());
        max.SetCpuUs(stats.Max.CpuValue.count());
        max.SetModule(TString{ModName(names, stats.Max.Mod)});

        FORCE_TRACE(M, msg)
    }
}

void TModuleStagesStatsManager::CheckUnreported() const {
    if (!Stages_.empty()) {
        YWarn() << Stages_.size() << " module statistics events were not reported!" << Endl;
    }
}

TModuleStagesStatsManager& TModuleStagesStatsManager::Current() {
    auto ctx = CurrentContext<TExecContext>;
    if (ctx && ctx->ModStatsManager) {
        return *ctx->ModStatsManager;
    }
    return *Singleton<TModuleStagesStatsManager>();
}

TFileElemId TModuleStagesStatsManager::FindCurrentModule() {
    return Diag()->Where.back().first;
}
