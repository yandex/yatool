#pragma once

#include <util/system/datetime.h>

#include <chrono>

/// A std::chrono-compatible clock that measures per-thread CPU time.
///
/// The clock is NOT comparable across threads.
class TThreadCPUClock {
public:
    using duration   = std::chrono::microseconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = std::chrono::time_point<TThreadCPUClock, duration>;

    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        return time_point{duration{static_cast<rep>(::ThreadCPUTime())}};
    }
};
