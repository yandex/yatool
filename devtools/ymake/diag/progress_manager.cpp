#include "progress_manager.h"

#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/context_executor.h>

#include <util/datetime/base.h>
#include <util/generic/vector.h>
#include <util/generic/singleton.h>
#include <util/system/types.h>

void TProgressManager::TRateChecker::Push(const ui64 size, const std::chrono::microseconds& loadTime) {
    SumOfData.Size -= Data[Index].Size;
    SumOfData.LoadTime -= Data[Index].LoadTime;

    Data[Index].Size = size;
    Data[Index].LoadTime = loadTime;
    Index = (Index + 1) % Data.size();

    SumOfData.Size += size;
    SumOfData.LoadTime += loadTime;
}

ui64 TProgressManager::TRateChecker::GetRate() const {
    return SumOfData.Size / (SumOfData.LoadTime.count() / 1e6);
}

void TProgressManager::ForceTrace(TInstant currentTime) {
    if (IsProgressEnabled) {
        FORCE_TRACE(G, FilesStat);
        FORCE_TRACE(G, ConfModulesStat);

        LastSendTime = currentTime;
    }
}

void TProgressManager::TryToTrace(TInstant currentTime) {
    if (IsProgressEnabled) {
        if ((currentTime - LastSendTime).MicroSeconds() < TimeBetweenSends) {
            return;
        }

        ForceTrace(currentTime);
    }
}

void TProgressManager::ForceTraceRenderStat(TInstant currentTime) {
    if (IsProgressEnabled) {
        FORCE_TRACE(G, RenderModulesStat);

        LastSendTime = currentTime;
    }
}

void TProgressManager::TryToTraceRenderStat(TInstant currentTime) {
    if (IsProgressEnabled) {
        if ((currentTime - LastSendTime).MicroSeconds() < TimeBetweenSends) {
            return;
        }

        ForceTraceRenderStat(currentTime);
    }
}

TProgressManager::TProgressManager(const ui32 rateCheckerSize, const ui32 timeBetweenSends)
    : RateChecker(rateCheckerSize)
    , TimeBetweenSends(timeBetweenSends)
    , IsProgressEnabled{NYMake::TraceEnabled(ETraceEvent::G)}
{
    FilesStat.SetCount(0);
    FilesStat.SetRate(0);
    ConfModulesStat.SetTotal(0);
    ConfModulesStat.SetDone(0);
    RenderModulesStat.SetTotal(0);
    RenderModulesStat.SetDone(0);
}

void TProgressManager::UpdateFilesData(const ui64 fileSize, const ui64 loadTime, const ui64 filesCount, const TInstant currentTime) {
    if (IsProgressEnabled) {
        RateChecker.Push(fileSize, std::chrono::microseconds(loadTime));

        FilesStat.SetCount(filesCount);
        FilesStat.SetRate(RateChecker.GetRate());

        TryToTrace(currentTime);
    }
}

void TProgressManager::IncConfModulesDone(const TInstant currentTime) {
    if (IsProgressEnabled) {
        ConfModulesStat.SetDone(ConfModulesStat.GetDone() + 1);

        TryToTrace(currentTime);
    }
}

void TProgressManager::UpdateConfModulesTotal(const ui64 modulesTotal, const TInstant currentTime) {
    if (IsProgressEnabled) {
        ConfModulesStat.SetTotal(modulesTotal);

        TryToTrace(currentTime);
    }
}

void TProgressManager::ForceUpdateConfModulesDoneTotal(const ui64 modulesDone, const ui64 modulesTotal, TInstant currentTime) {
    if (IsProgressEnabled) {
        ConfModulesStat.SetDone(modulesDone);
        ConfModulesStat.SetTotal(modulesTotal);

        ForceTrace(currentTime);
    }
}

void TProgressManager::ForceUpdateRenderModulesTotal(const ui64 modulesTotal, const TInstant currentTime) {
    if (IsProgressEnabled) {
        RenderModulesStat.SetTotal(modulesTotal);

        ForceTraceRenderStat(currentTime);
    }
}

void TProgressManager::IncRenderModulesDone(const TInstant currentTime) {
    if (IsProgressEnabled) {
        RenderModulesStat.SetDone(RenderModulesStat.GetDone() + 1);

        TryToTraceRenderStat(currentTime);
    }
}

void TProgressManager::ForceRenderModulesDone(const TInstant currentTime) {
    if (IsProgressEnabled) {
        ForceTraceRenderStat(currentTime);
    }
}

TProgressManager* Instance() {
    auto ctx = CurrentContext<TExecContext>;
    if (ctx && ctx->ProgressManager) {
        return ctx->ProgressManager.get();
    }
    return Singleton<TProgressManager>();
}
