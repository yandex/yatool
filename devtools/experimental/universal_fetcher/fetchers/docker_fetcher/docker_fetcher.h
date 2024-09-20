#pragma once

#include <devtools/experimental/universal_fetcher/universal_fetcher/fetchers_interface.h>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    class IDockerFetcherMetrics {
    public:
        virtual ~IDockerFetcherMetrics() {
        }
    };

    struct TDockerFetcherConfig {
        TString AuthJsonFile; // auth_json_file
        TDuration Timeout = TDuration::Minutes(5);

        static TDockerFetcherConfig FromJson(const NJson::TJsonValue&);
    };

    TFetcherPtr CreateDockerFetcher(const TDockerFetcherConfig&, const TProcessRunnerPtr& runner, IDockerFetcherMetrics* metrics = nullptr);

}
