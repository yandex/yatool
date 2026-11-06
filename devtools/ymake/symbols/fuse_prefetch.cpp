#include "fuse_prefetch.h"

#include <util/system/file.h>

#include <asio/post.hpp>
#include <asio/thread_pool.hpp>

#include <exception>
#include <future>
#include <latch>

namespace NFusePrefetch {
    namespace {
        constexpr size_t PoolSize = 16;
        constexpr size_t MaxPendingBytes = 256 << 20;

        asio::thread_pool& StatPool() {
            static asio::thread_pool pool{PoolSize};
            return pool;
        }

        asio::thread_pool& ReadPool() {
            static asio::thread_pool pool{PoolSize};
            return pool;
        }

        TBlob ReadFile(const TString& realPath) {
            try {
                return TBlob::FromFileContent(TFile(realPath, RdOnly | Seq));
            } catch (...) {
                return TBlob();
            }
        }
    }

    struct TPrefetcher::TJob {
        TString Path;
        size_t Size;
        std::atomic_flag Claimed;
        std::promise<TBlob> Result;
    };

    void ParallelFor(size_t n, const std::function<void(size_t)>& fn) {
        if (n < 2) {
            for (size_t i = 0; i < n; ++i) {
                fn(i);
            }
            return;
        }
        const size_t parts = Min(n, PoolSize);
        auto done = std::make_shared<std::latch>(parts);
        std::exception_ptr error;
        std::mutex errorLock;
        for (size_t p = 0; p < parts; ++p) {
            asio::post(StatPool(), [&, p, done]() {
                try {
                    for (size_t i = p; i < n; i += parts) {
                        fn(i);
                    }
                } catch (...) {
                    std::lock_guard g(errorLock);
                    if (!error) {
                        error = std::current_exception();
                    }
                }
                done->count_down();
            });
        }
        done->wait();
        if (error) {
            std::rethrow_exception(error);
        }
    }

    TPrefetcher::~TPrefetcher() {
        Finish();
    }

    void TPrefetcher::Prefetch(const TString& realPath, size_t size) {
        auto job = std::make_shared<TJob>();
        job->Path = realPath;
        job->Size = size;
        {
            std::lock_guard g(Lock_);
            if (Finished_) {
                return;
            }
            if (PendingBytes_ + size > MaxPendingBytes) {
                ++OverLimit_;
                return;
            }
            if (!InFlight_.emplace(realPath, job).second) {
                return;
            }
            PendingBytes_ += size;
        }
        ++Started_;
        StartedBytes_ += size;
        asio::post(ReadPool(), [job, reads = Reads_]() {
            if (!job->Claimed.test_and_set()) {
                job->Result.set_value(ReadFile(job->Path));
                ++*reads;
            }
        });
    }

    bool TPrefetcher::Take(const TString& realPath, TBlob& blob) {
        std::shared_ptr<TJob> job;
        {
            std::lock_guard g(Lock_);
            auto it = InFlight_.find(realPath);
            if (it == InFlight_.end()) {
                ++Misses_;
                return false;
            }
            job = std::move(it->second);
            PendingBytes_ -= job->Size;
            InFlight_.erase(it);
        }
        if (!job->Claimed.test_and_set()) {
            ++Misses_;
            return false;
        }
        blob = job->Result.get_future().get();
        if (blob.Empty()) {
            ++Misses_;
            return false;
        }
        ++Hits_;
        return true;
    }

    void TPrefetcher::Finish() {
        std::lock_guard g(Lock_);
        if (Finished_) {
            return;
        }
        Finished_ = true;
        for (auto& [_, job] : InFlight_) {
            job->Claimed.test_and_set();
        }
        Unused_ = InFlight_.size();
        UnusedBytes_ = PendingBytes_;
        InFlight_.clear();
        PendingBytes_ = 0;
    }

    TStats TPrefetcher::Stats() const {
        return {Started_.load(), StartedBytes_.load(), Reads_->load(), Hits_.load(), Misses_.load(), OverLimit_.load(), Unused_.load(), UnusedBytes_.load()};
    }
}
