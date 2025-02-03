#pragma once

#include <util/generic/size_literals.h>
#include <devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    class IDockerFetcherMetrics {
    public:
        virtual void OnFailedToGetImageSize() {};
        virtual void OnTooLargeImage() {};

        virtual ~IDockerFetcherMetrics() {
        }
    };

    struct TDockerFetcherConfig {
        TString SkopeoBinaryPath = "skopeo"; // name (if under PATH) or full path
        TString AuthJsonFile; // auth_json_file
        TDuration Timeout = TDuration::Minutes(5);
        ui64 ImageSizeLimit = 3_GB;

        TVector<TString> _SkopeoArgs = {};  // only for testing !!!

        static TDockerFetcherConfig FromJson(const NJson::TJsonValue&);
    };

    TFetcherPtr CreateDockerFetcher(const TDockerFetcherConfig&, const TProcessRunnerPtr& runner, IDockerFetcherMetrics* metrics = nullptr);

}
