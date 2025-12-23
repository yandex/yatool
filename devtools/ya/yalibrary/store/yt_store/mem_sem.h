#pragma once

#include <util/generic/scope.h>
#include <util/system/condvar.h>
#include <util/system/types.h>


namespace NYa {
    class TMemorySemaphoreGuard;

    class TMemorySemaphore : public TNonCopyable {
    public:
        explicit TMemorySemaphore(std::size_t maxMemoryUsage);
        std::unique_ptr<TMemorySemaphoreGuard> Acquire(std::size_t memSize) noexcept;

    private:
        void ReAcquire(std::size_t prevMemSize, std::size_t newMemSize) noexcept;
        void Release(std::size_t memSize) noexcept;

        friend TMemorySemaphoreGuard;

        TMutex Mutex_{};
        TCondVar CondVar_{};
        std::ptrdiff_t FreeMem_{};
        int Threads_{};
    };

    class TMemorySemaphoreGuard : public TNonCopyable {
    public:
        TMemorySemaphoreGuard(TMemorySemaphore& parent, std::size_t memSize);
        ~TMemorySemaphoreGuard();

        // Overcommit allowed
        void ReAcquire(std::size_t newMemSize) noexcept;
    private:
        TMemorySemaphore& Parent_;
        std::size_t MemSize_;
    };
}
