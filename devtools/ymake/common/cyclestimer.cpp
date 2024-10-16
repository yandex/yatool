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
    return GetSeconds() * 1000000ull;
}

double TCyclesTimer::GetSeconds() const {
    return double(::GetCycleCount() - Start_) / NHPTimer::GetCyclesPerSecond();
}

TCyclesTimerRestarter::TCyclesTimerRestarter(TCyclesTimer& cyclesTimer)
    : CyclesTimer_(cyclesTimer)
{}

TCyclesTimerRestarter::~TCyclesTimerRestarter() {
    CyclesTimer_.Restart();
}
