#pragma once

#include <devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    struct TCustomFetcherParams {
        TString PathToFetcher;

        static TCustomFetcherParams FromJson(const NJson::TJsonValue&);
    };

    TFetcherPtr CreateCustomFetcher(const TCustomFetcherParams&, const TProcessRunnerPtr& runner);

}
