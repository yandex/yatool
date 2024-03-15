#pragma once

#include <util/system/platform.h>
#include <util/generic/string.h>
#include <stdint.h>
#include <functional>
#if defined(_win_)
#include <util/system/winint.h>
#endif

namespace NProcUtil {
    class TSubreaperResource {
    public:
        void Close();
    #if defined(_win_)
        explicit TSubreaperResource(HANDLE jh);
    #else
        explicit TSubreaperResource();
    #endif
#if defined(_win_)
    private:
        HANDLE JobHandle;
#endif
    };
    void TerminateChildren();
    TSubreaperResource BecomeSubreaper();
#if defined(_linux_)
    bool LinuxBecomeSubreaper(std::function<void()> cleanupAfterFork = []() -> void {});
    void UnshareNs();
#elif defined(_win_)
    void* WinCreateSubreaperJob();
#endif
}
