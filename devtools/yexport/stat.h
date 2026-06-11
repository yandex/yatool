#pragma once

#include <devtools/ymake/libs/clocks/checkpoint.h>
#include <devtools/ymake/libs/clocks/hp_clock.h>

#include <util/generic/ptr.h>

#include <string>
#include <list>
#include <limits>

namespace NYexport {
    struct TStage {
        std::string Name;
        int Calls{0}; // count of stage calls
        double SumSec{0}; // summary execution time in seconds
        double MinSec{std::numeric_limits<double>::max()}; // minimal execution time in seconds
        double AvrSec{0}; // average execution time in seconds
        double MaxSec{0}; // maximum execution time in seconds
        std::list<TStage> SubStages;
    };

    class TStageCall {
    public:
        TStageCall(std::string_view stagePath);
        TStageCall(const char* stagePath);
        ~TStageCall();
    private:
        TStage& Stage_;
        TCheckPoint<THPClock> Checkpoint_;
    };
}

void PrintStagesStat();
