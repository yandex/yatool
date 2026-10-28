#pragma once

#include <devtools/local_cache/ac/proto/ac.pb.h>
#include <devtools/shared_cache/proto/telemetry.pb.h>
#include <library/cpp/logger/log.h>
#include <util/datetime/base.h>
#include <util/generic/ptr.h>

#include <functional>

namespace NACCache::NDistCache {
    struct TOptions {
        TString Url;
        TString CacheDir;
        size_t BatchSize = 500;
        size_t MaxQueueItems = 10000;
        // Provisional, configurable backlog budget, including the in-flight batch.
        // Independent of the item limit; serialization/transport memory is extra.
        size_t MaxQueueBytes = 64 * 1024 * 1024;
        size_t MaxAttempts = 3;
        TDuration FlushInterval = TDuration::Seconds(10);
        TDuration RequestTimeout = TDuration::Seconds(2);
        TDuration RetryDelay = TDuration::MilliSeconds(200);
        TDuration Cooldown = TDuration::Minutes(5);
        TDuration DrainTimeout = TDuration::Seconds(30);
    };

    enum class ESendResult { Success,
                             Retry,
                             Rejected };
    struct TTransport {
        std::function<ESendResult(const sharedcache::v1::AnnounceRequest&)> Announce;
    };

    // Network, serialization and filesystem observations run on
    // the worker. Outstanding limits include the batch currently being sent.
    class TAnnouncer {
    public:
        TAnnouncer(TOptions options, TLog& log, TTransport transport = {});
        ~TAnnouncer();

        void Enqueue(TPutUid&& request) noexcept;
        bool IsIdle() const noexcept;
        void Stop() noexcept;
        const TString& Url() const noexcept;

    private:
        class TImpl;
        THolder<TImpl> Impl_;
    };
} // namespace NACCache::NDistCache
