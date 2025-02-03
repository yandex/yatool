#pragma once

#include <devtools/libs/universal_fetcher/universal_fetcher/universal_fetcher.h>

#include <functional>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    using TUniversalFetcherFetcherCtor = std::function<TFetcherPtr(const NJson::TJsonValue&, const TProcessRunnerPtr& runner)>;

    void RegisterFetcher(TUniversalFetcherFetcherCtor ctor, const TString& name, const TVector<TString>& schemas);

    void RegisterProcessRunner(TProcessRunnerPtr& runner);

    THolder<TUniversalFetcher> CreateUniversalFetcher(const NJson::TJsonValue& config);

    using TUniversalFetcherLoggingFunction = std::function<void(ELogPriority, const TString&)>;

    THolder<TUniversalFetcher> CreateUniversalFetcher(const TString& configJson, TUniversalFetcherLoggingFunction logFunction);
    THolder<TUniversalFetcher> CreateUniversalFetcher(const TString& configJson);

}

