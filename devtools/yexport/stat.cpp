#include "stat.h"

#include <devtools/yexport/diag/trace.h>

#include <spdlog/spdlog.h>

using namespace NYexport;

namespace {
    class TStagesStat {
    public:
        static const char STAGE_PATH_DIVIDER = '>';

        TStagesStat() = default;
        TStage& CreateStage(std::string_view stagePath) {
            size_t epos = 0;
            TStage* currentStage = &RootStage_;
            if (!stagePath.empty()) {
                do {
                    epos = stagePath.find(STAGE_PATH_DIVIDER, epos + 1);
                    auto stageName = stagePath.substr(0, epos);
                    TStage* foundStage{nullptr};
                    for (auto& subStage: currentStage->SubStages) {
                        if (subStage.Name == stageName) {
                            foundStage = &subStage;
                            break;
                        }
                    }
                    if (foundStage) {
                        currentStage = foundStage;
                    } else {
                        auto& newStage = currentStage->SubStages.emplace_back();
                        newStage.Name = stageName;
                        currentStage = &newStage;
                    }
                } while (epos != std::string_view::npos);
            }
            return *currentStage;
        }

        void TraceAndPrint() {
            FillStage(RootStage_);
            TraceSubStages(RootStage_);
            spdlog::info("--- Stages stat");
            spdlog::info("Format: <Stage> <Summary> sec [<Min> sec, <Avr> sec, <Max> sec], <Count> times");
            PrintSubStages(RootStage_);
            spdlog::info("=== {:.3f} sec", Timer_.GetSeconds());
        }

    private:
        TStage RootStage_;
        TCyclesTimer Timer_;

        void FillStage(TStage& stage) {
            FillSubStages(stage);
            if (!stage.Calls) {
                for (const auto& subStage: stage.SubStages) {
                    stage.SumSec += subStage.SumSec;
                }
                stage.Calls = 1;
                stage.MinSec = stage.SumSec;
                stage.AvrSec = stage.SumSec;
                stage.MaxSec = stage.SumSec;
            } else {
                stage.AvrSec = stage.SumSec / (double)stage.Calls;
            }
        }

        void FillSubStages(TStage& stage) {
            for (auto& subStage: stage.SubStages) {
                FillStage(subStage);
            }
        }

        void PrintStage(TStage& stage) {
            PrintSubStages(stage);
            if (stage.Calls > 1) {
                spdlog::info("Stat: {} {:.3f} sec [{:.3f} sec, {:.3f} sec, {:.3f} sec], {:d} times", stage.Name, stage.SumSec, stage.MinSec, stage.AvrSec, stage.MaxSec, stage.Calls);
            } else {
                spdlog::info("Stat: {} {:.3f} sec", stage.Name, stage.SumSec);
            }
        }

        void PrintSubStages(TStage& stage) {
            for (auto& subStage: stage.SubStages) {
                PrintStage(subStage);
            }
        }

        void TraceStage(TStage& stage) {
            TraceSubStages(stage);
            TraceStageStat(stage.Name, stage.SumSec, stage.Calls, stage.MinSec, stage.AvrSec, stage.MaxSec);
        }

        void TraceSubStages(TStage& stage) {
            for (auto& subStage: stage.SubStages) {
                TraceStage(subStage);
            }
        }
    };

    static TStagesStat StagesStat;
}

TStageCall::TStageCall(std::string_view stagePath)
    : Stage_(StagesStat.CreateStage(stagePath))
    , Timer_()
{}

TStageCall::TStageCall(const char* stagePath) : TStageCall(std::string_view(stagePath))
{}

TStageCall::~TStageCall() {
    auto sec = Timer_.GetSeconds();
    Stage_.Calls++;
    Stage_.SumSec += sec;
    if (sec < Stage_.MinSec) {
        Stage_.MinSec = sec;
    }
    if (sec > Stage_.MaxSec) {
        Stage_.MaxSec = sec;
    }
}

void PrintStagesStat() {
    StagesStat.TraceAndPrint();
}
