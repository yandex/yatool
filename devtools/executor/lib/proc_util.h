#pragma once

#include <util/system/platform.h>
#include <util/generic/string.h>
#include <stdint.h>
#include <functional>

namespace NProcUtil {
    void TerminateChildren();
#if defined(_linux_)
    bool LinuxBecomeSubreaper(std::function<void()> cleanupAfterFork = []() -> void {});
    void UnshareNs();
#elif defined(_win_)
    void* WinCreateSubreaperJob();
#endif
}
