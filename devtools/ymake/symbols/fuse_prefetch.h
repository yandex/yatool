#pragma once

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/memory/blob.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// Hides per-syscall FUSE round trips behind thread pools, only in directories with slow lstat.
// Enabled by FUSE_PREFETCH and FUSE_PREFETCH_EXTS in core.conf, see TFileConf::SetFusePrefetch.
namespace NFusePrefetch {
    // Runs fn(i) for i in [0, n) on the stat pool and waits for all; rethrows the first exception.
    void ParallelFor(size_t n, const std::function<void(size_t)>& fn);

    struct TStats {
        size_t Started = 0;
        size_t StartedBytes = 0;
        size_t Reads = 0;
        size_t Hits = 0;
        size_t Misses = 0;
        size_t OverLimit = 0;
        size_t Unused = 0;
        size_t UnusedBytes = 0;
    };

    class TPrefetcher {
    public:
        ~TPrefetcher();

        // Queues a background read of a small file; the result is kept until Take() or Finish().
        void Prefetch(const TString& realPath, size_t size);

        // Takes the blob of a read that has started, waiting for it to finish. False if there is none: the caller reads the file itself.
        bool Take(const TString& realPath, TBlob& blob);

        // Cancels queued reads and drops unclaimed blobs.
        void Finish();

        TStats Stats() const;

    private:
        struct TJob;

        std::mutex Lock_;
        THashMap<TString, std::shared_ptr<TJob>> InFlight_;
        size_t PendingBytes_ = 0;
        bool Finished_ = false;
        std::atomic<size_t> Started_ = 0, StartedBytes_ = 0, Hits_ = 0, Misses_ = 0, OverLimit_ = 0, Unused_ = 0, UnusedBytes_ = 0;
        std::shared_ptr<std::atomic<size_t>> Reads_ = std::make_shared<std::atomic<size_t>>(0);
    };
}
