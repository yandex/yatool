#include "reclaim.h"

#include <semaphore>

#if defined(_linux_)
    #include <sys/resource.h>
#endif

namespace {
    struct TOpenedFilesSemaphore {
        void AcquireSlot() noexcept;
        void ReleaseSlot() noexcept;
        static size_t ComputeFileLimit();

#if defined(_linux_)
        static constexpr size_t MIN_VALUE_SEMAPHORE = 20;
        static constexpr size_t MAX_VALUE_SEMAPHORE = 200;
        std::counting_semaphore<MAX_VALUE_SEMAPHORE> Semaphore_;
        explicit TOpenedFilesSemaphore(size_t init)
            : Semaphore_(std::clamp<size_t>(init, MIN_VALUE_SEMAPHORE, MAX_VALUE_SEMAPHORE))
        {
        }
#else
        explicit TOpenedFilesSemaphore(size_t) {
        }
#endif

    } OpenedFilesSemaphore_(TOpenedFilesSemaphore::ComputeFileLimit());
} // namespace

#if defined(_linux_)
void TOpenedFilesSemaphore::AcquireSlot() noexcept {
    Semaphore_.acquire();
}
void TOpenedFilesSemaphore::ReleaseSlot() noexcept {
    Semaphore_.release();
}
size_t TOpenedFilesSemaphore::ComputeFileLimit() {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == 0) {
        return 100;
    }
    size_t threeQuarters = static_cast<size_t>(rl.rlim_cur) * 3 / 4;
    return threeQuarters > 100 ? threeQuarters : 100;
}
#else
void TOpenedFilesSemaphore::AcquireSlot() noexcept {
}
void TOpenedFilesSemaphore::ReleaseSlot() noexcept {
}
size_t TOpenedFilesSemaphore::ComputeFileLimit() {
    return 1;
}
#endif

WrappedFile::TSlotToken::TSlotToken() {
    OpenedFilesSemaphore_.AcquireSlot();
}

WrappedFile::TSlotToken::~TSlotToken() {
    OpenedFilesSemaphore_.ReleaseSlot();
}
