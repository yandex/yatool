#pragma once

#include <devtools/ymake/diag/trace.ev.pb.h>

#include <util/datetime/base.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

class TProgressManager {
private:
    class TRateChecker {
    private:
        struct TReadStats {
            TReadStats(const ui64 size = 0, const std::chrono::microseconds& loadTime = std::chrono::microseconds(0))
                : Size(size)
                , LoadTime(loadTime)
            {
            }
            ui64 Size;
            std::chrono::microseconds LoadTime;
        };

    private:
        TVector<TReadStats> Data;
        TReadStats SumOfData;
        ui32 Index;

    public:
        TRateChecker(const ui32 size)
            : Data(size)
            , Index(0)
        {
        }

        void Push(const ui64 size, const std::chrono::microseconds& loadTime);
        ui64 GetRate() const; // Bytes per seconds
    };

private:
    TInstant LastSendTime;
    TRateChecker RateChecker;
    const ui64 TimeBetweenSends;
    NEvent::TFilesStat FilesStat;
    NEvent::TConfModulesStat ConfModulesStat;
    NEvent::TRenderModulesStat RenderModulesStat;

    bool IsProgressEnabled = false;

private:
    void ForceTrace(const TInstant currentTime = Now());
    void TryToTrace(const TInstant currentTime = Now());
    void ForceTraceRenderStat(const TInstant currentTime = Now());
    void TryToTraceRenderStat(const TInstant currentTime = Now());

public:
    TProgressManager(const ui32 rateCheckerSize = 50, const ui32 timeBetweenSends = TInstant::MilliSeconds(200).MicroSeconds());

    void UpdateFilesData(const ui64 fileSize, const ui64 loadTime, const ui64 filesCount, const TInstant currentTime = Now());

    void IncConfModulesDone(const TInstant currentTime = Now());
    void UpdateConfModulesTotal(const ui64 modulesTotal, const TInstant currentTime = Now());
    void ForceUpdateConfModulesDoneTotal(const ui64 modulesDone, const ui64 modulesTotal, TInstant currentTime = Now());

    void ForceUpdateRenderModulesTotal(const ui64 modulesTotal, const TInstant currentTime = Now());
    void IncRenderModulesDone(const TInstant currentTime = Now());
    void ForceRenderModulesDone(const TInstant currentTime = Now());
};

TProgressManager* Instance();
