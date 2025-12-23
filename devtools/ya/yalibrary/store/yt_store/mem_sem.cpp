#include "mem_sem.h"

namespace NYa {
    TMemorySemaphore::TMemorySemaphore(std::size_t maxMemoryUsage)
        : FreeMem_{static_cast<std::ptrdiff_t>(maxMemoryUsage)}
    {
    }

    std::unique_ptr<TMemorySemaphoreGuard> TMemorySemaphore::Acquire(std::size_t memSize) noexcept {
        TGuard<TMutex> guard{Mutex_};

        // Allow a single thread to allocate any amount of memory
        while (Threads_ > 0 && FreeMem_ < static_cast<std::ptrdiff_t>(memSize)) {
            CondVar_.Wait(Mutex_);
        }
        ++Threads_;
        FreeMem_ -= static_cast<std::ptrdiff_t>(memSize);

        return std::make_unique<TMemorySemaphoreGuard>(*this, memSize);
    }

    void TMemorySemaphore::ReAcquire(std::size_t prevMemSize, std::size_t newMemSize) noexcept {
        if (prevMemSize == newMemSize) {
            return;
        }
        TGuard<TMutex> guard{Mutex_};

        FreeMem_ += static_cast<std::ptrdiff_t>(prevMemSize) - static_cast<std::ptrdiff_t>(newMemSize);
        CondVar_.BroadCast();
    }

    void TMemorySemaphore::Release(std::size_t memSize) noexcept {
        TGuard<TMutex> guard{Mutex_};

        --Threads_;
        FreeMem_ += static_cast<std::ptrdiff_t>(memSize);
        CondVar_.BroadCast();
    }

    TMemorySemaphoreGuard::TMemorySemaphoreGuard(TMemorySemaphore& parent, std::size_t memSize)
        : Parent_{parent}
        , MemSize_{memSize}
    {
    }

    TMemorySemaphoreGuard::~TMemorySemaphoreGuard() {
        Parent_.Release(MemSize_);
    }

    void TMemorySemaphoreGuard::ReAcquire(std::size_t newMemSize) noexcept {
        Parent_.ReAcquire(MemSize_, newMemSize);
        MemSize_ = newMemSize;
    }
}
