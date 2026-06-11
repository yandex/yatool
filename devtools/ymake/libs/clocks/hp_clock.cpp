#include "hp_clock.h"
#include "checkpoint.h"

#include <util/system/datetime.h>
#include <util/system/hp_timer.h>

namespace {

static ui64 CyclesPerSecond = NHPTimer::GetCyclesPerSecond();

};

THPClock::time_point THPClock::now() {
    return time_point{duration{double(::GetCycleCount())/CyclesPerSecond}};
}

template<>
TCheckPoint<THPClock> MakeCheckpoint<THPClock>() {
    return TCheckPoint<THPClock>{::GetCycleCount()};
}

template<>
THPClock::duration TimeSince<THPClock>(TCheckPoint<THPClock> checkPoint) {
    return TDoubleSeconds{double(::GetCycleCount() - checkPoint.Checkpoint_)/CyclesPerSecond};
}
