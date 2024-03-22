#pragma once

#include <stdint.h>
#include <functional>

#include <util/system/platform.h>
#include <util/generic/string.h>
#include <util/system/winint.h>

namespace NProcUtil {
    class TSubreaperApplicant {
        public:
            explicit TSubreaperApplicant();
            void Close();
#if defined(_win_)
        private:
            HANDLE JobHandle;
#endif
        };

    void TerminateChildren();
#if defined(_linux_)
    bool LinuxBecomeSubreaper(std::function<void()> cleanupAfterFork = []() -> void {});
    void UnshareNs();
#elif defined(_win_)
    void* WinCreateSubreaperJob();
#endif
}
