#pragma once

#include "dst_path.h"

#include <devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h>

#include <library/cpp/logger/priority.h>
#include <library/cpp/logger/log.h>
#include <library/cpp/json/json_value.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/datetime/base.h>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    struct IFetcherMetrics {
        virtual ~IFetcherMetrics() {
        }

        virtual void OnRequest() {}
        virtual void OnRequestOk(TDuration) {}
        virtual void OnRequestFailed(TDuration) {}
        virtual void OnFetchAttemptFinished(EFetchStatus) {};
    };

    class TUniversalFetcher {
    public:
        struct TRetriesConfig {
            // Maximum number of retry attempts
            int MaxRetryCount = 0;
            // Initial delay before the first retry
            TDuration InitialDelay = TDuration::MilliSeconds(100);
            // Multiplier for exponential backoff
            double BackoffMultiplier = 1.5;
            // Maximum delay between retries
            TDuration MaxDelay = TDuration::Seconds(1);
            // Whether to add random jitter to the delay
            bool Jitter = true;
            // Whether to use a fixed delay between retries
            bool UseFixedDelay = false;

            static TRetriesConfig FromJson(const NJson::TJsonValue&);
        };

        struct TFetcherWithParams {
            TFetcherPtr Fetcher;
            TRetriesConfig Retries = {};
            IFetcherMetrics* Metrics = nullptr;
        };

        struct TParams {
            THashMap<TString, TFetcherWithParams> Fetchers;
        };

        TUniversalFetcher(const TParams& params, TLogBackendPtr logBackend = nullptr);

        struct TFetchAttempt {
            TInstant StartTime;
            TDuration Duration;
            TFetchResult Result;

            void ToJson(NJson::TJsonValue&) const;
        };

        struct TResult {
            TFetchAttempt LastAttempt;
            TVector<TFetchAttempt> History;

            NJson::TJsonValue ToJson() const;
            TString ToJsonString() const;

            bool IsSuccess() const;
            TString GetError() const;
        };
        using TResultPtr = THolder<TResult>;

    public:
        TResultPtr Download(const TString& uri, const TDstPath& dstPath, const TFetchParams& params = TFetchParams(), TCancellationToken cancellation = TCancellationToken::Default()) noexcept;
        TResultPtr DownloadToDir(const TString& uri, const TString& dstDir, const TFetchParams& params = TFetchParams(), TCancellationToken cancellation = TCancellationToken::Default()) noexcept;
        TResultPtr DownloadToFile(const TString& uri, const TString& dstFile, const TFetchParams& params = TFetchParams(), TCancellationToken cancellation = TCancellationToken::Default()) noexcept;

    private:
        THashMap<TString, TFetcherWithParams> Fetchers_;
        TLog Log_;
        static IFetcherMetrics DummyMetrics_;
    };

    using TUniversalFetcherResult = TUniversalFetcher::TResult;

}
