#pragma once

#include <util/system/types.h>

class TCyclesTimer {
public:
    TCyclesTimer();
    void Restart();
    size_t GetUs() const;
    double GetSeconds() const;
private:
    ui64 Start_;
};

class TCyclesTimerRestarter {
public:
    TCyclesTimerRestarter(TCyclesTimer& cyclesTimer);
    ~TCyclesTimerRestarter();
private:
    TCyclesTimer& CyclesTimer_;
};
