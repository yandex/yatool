#pragma once

#include <devtools/ymake/libs/clocks/hp_clock.h>

#include <util/system/types.h>

template<typename Clock>
class TCheckPoint;
template<typename Clock>
typename Clock::duration TimeSince(TCheckPoint<Clock> checkPoint);

template<typename Clock>
class TCheckPoint {
public:
    constexpr TCheckPoint() noexcept = default;
    constexpr TCheckPoint(typename Clock::time_point ts) noexcept
        : Timestamp_{ts}
    {}

    friend typename Clock::duration TimeSince<Clock>(TCheckPoint<Clock> checkPoint);
private:
    typename Clock::time_point Timestamp_ = {};
};

template<typename Clock>
TCheckPoint<Clock> MakeCheckpoint() {
    return TCheckPoint<Clock>{Clock::now()};
}

template<typename Clock>
typename Clock::duration TimeSince(TCheckPoint<Clock> checkPoint) {
    return Clock::now() - checkPoint.Timestamp_;
}

// Specializations fot High Precision time measurements

template<>
TCheckPoint<THPClock> MakeCheckpoint<THPClock>();
template<>
THPClock::duration TimeSince<THPClock>(TCheckPoint<THPClock> checkPoint);


template<>
class TCheckPoint<THPClock> {
public:
    friend TCheckPoint<THPClock> MakeCheckpoint<THPClock>();
    friend THPClock::duration TimeSince<THPClock>(TCheckPoint<THPClock>);

private:
    TCheckPoint(ui64 start) noexcept
        : Checkpoint_{start}
    {}

private:
    ui64 Checkpoint_ = 0;
};
