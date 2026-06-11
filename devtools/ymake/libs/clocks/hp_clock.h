#pragma once

#include <devtools/ymake/libs/clocks/fractional_duration.h>

#include <chrono>

class THPClock {
public:
    using duration = TDoubleSeconds;
    using time_point = std::chrono::time_point<THPClock, duration>;
    using rep = duration::rep;

    static constexpr bool is_steady = true;

    static time_point now();
};
