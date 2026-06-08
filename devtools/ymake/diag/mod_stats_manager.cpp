#include "mod_stats_manager.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/trace.ev.pb.h>

#include <devtools/ymake/libs/name_store/name_store.h>

#include <devtools/ymake/context_executor.h>

namespace NDetail {

TScopedMeasurer::TScopedMeasurer(TModStageStats& dest, TFileElemId mod) noexcept
    : Start_{std::chrono::steady_clock::now()}
    , Mod_{mod}
    , Dest_{dest}
{}

TScopedMeasurer::~TScopedMeasurer() noexcept {
    const auto time = std::chrono::steady_clock::now() - Start_;
    ++Dest_.Count;
    Dest_.Total += time;
    if (time < Dest_.Min.Value) {
        Dest_.Min.Mod = Mod_;
        Dest_.Min.Value = time;
    }
    if (time > Dest_.Max.Value) {
        Dest_.Max.Mod = Mod_;
        Dest_.Max.Value = time;
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
        msg.SetTotalUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Total).count());
        msg.SetCount(stats.Count);

        TString modName;

        msg.SetMinUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Min.Value).count());
        modName = TString{names.GetStringBufName(RawElemId(stats.Min.Mod))};
        msg.SetMinModule(modName);

        msg.SetMaxUs(std::chrono::duration_cast<std::chrono::microseconds>(stats.Max.Value).count());
        modName = TString{names.GetStringBufName(RawElemId(stats.Max.Mod))};
        msg.SetMaxModule(modName);

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
