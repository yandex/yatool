#include "cyclestimer.h"

#include <util/system/datetime.h>
#include <util/system/hp_timer.h>

TCyclesTimer::TCyclesTimer()
    : Start_(::GetCycleCount())
{}

void TCyclesTimer::Restart() {
    *this = TCyclesTimer();
}

size_t TCyclesTimer::GetUs() const {
    return ((::GetCycleCount() - Start_) * 1000000ull) / NHPTimer::GetCyclesPerSecond();
}

TCyclesTimerRestarter::TCyclesTimerRestarter(TCyclesTimer& cyclesTimer)
    : CyclesTimer_(cyclesTimer)
{}

TCyclesTimerRestarter::~TCyclesTimerRestarter() {
    CyclesTimer_.Restart();
}
