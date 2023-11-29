#pragma once

#include <util/generic/noncopyable.h>
#include <library/cpp/deprecated/atomic/atomic.h>

#include <condition_variable>

namespace NACCache {
    struct TGCState {
        TAtomic* TotalFSSizeInOut = nullptr;
        TAtomic* TotalSizeInOut = nullptr;
        TAtomic* TotalACsInOut = nullptr;
        TAtomic* TotalBlobsInOut = nullptr;
        TAtomic* LastAccessTime = nullptr;
        ssize_t DiskLimitIn = 0;
        ssize_t DBSizeIn = 0;

        ssize_t DiskFSSizeDiffOut = 0;
        ssize_t DiskSizeDiffOut = 0;
        TAtomic CompletedOut = 0;
        i64 LastAccessInOut = 0;
    };

    struct TRefCountUpdateState {
        TAtomic CompletedOut = 0;
    };

    /// Interaction between GC and main thread.
    struct TCancelCallback : TNonCopyable {
        TCancelCallback(TAtomic* totalFSSize, TAtomic* totalSize, TAtomic* totalAcs, TAtomic* totalBlobs, TAtomic* LastAccessTime)
            : GCState({totalFSSize, totalSize, totalAcs, totalBlobs, LastAccessTime})
        {
        }

        void ResetGC(ssize_t limit, ssize_t dbSize) noexcept {
            GCState.DiskLimitIn = limit;
            GCState.DBSizeIn = dbSize;
            GCState.DiskFSSizeDiffOut = 0;
            GCState.DiskSizeDiffOut = 0;
            AtomicSet(GCState.CompletedOut, 0);
            AtomicSet(GCState.LastAccessInOut, 0);
        }

        void ResetRC() noexcept {
            AtomicSet(RCState.CompletedOut, 0);
        }

        bool IsWakeUpConditionMet() const noexcept {
            return AtomicGet(PendingRequests) == 0 || AtomicGet(ShutdownSignaled);
        }

        bool IsCancellationPending() const noexcept {
            return AtomicGet(PendingRequests) > 0 || AtomicGet(ShutdownSignaled);
        }

        bool IsShutdownPending() const noexcept {
            return AtomicGet(ShutdownSignaled);
        }

        bool IsLimitReached() const noexcept {
            return AtomicGet(*GCState.TotalFSSizeInOut) + GCState.DBSizeIn <= GCState.DiskLimitIn;
        }

        struct TGCState GCState;
        struct TRefCountUpdateState RCState;
        TAtomic PendingRequests = 0;
        TAtomic ShutdownSignaled = 0;

        mutable std::mutex LockWaitFlag;
        mutable std::condition_variable WaitFlag;
    };
}
