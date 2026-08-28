#pragma once

#include <stdint.h>
#include <functional>

#include <util/system/platform.h>
#include <util/system/winint.h>

namespace NProcUtil {
    enum class ENetworkIsolationStrategy {
        Direct,
        AppArmorRootlesskit,
        Unsupported,
    };

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
    ENetworkIsolationStrategy DetectNetworkIsolationStrategy();
    void UnshareNs(ENetworkIsolationStrategy strategy);
#elif defined(_win_)
    void* WinCreateSubreaperJob();
#endif
} // namespace NProcUtil
