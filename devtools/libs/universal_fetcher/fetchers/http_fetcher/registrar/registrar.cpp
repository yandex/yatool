#include <devtools/libs/universal_fetcher/fetchers/http_fetcher/http_fetcher.h>
#include <devtools/libs/universal_fetcher/registry/registry.h>


namespace NUniversalFetcher {

    namespace {
        struct TRegistrar {
            TRegistrar() {
                RegisterFetcher([](const NJson::TJsonValue& config, const TProcessRunnerPtr&) -> TFetcherPtr {
                    return CreateHttpFetcher(THttpFetcherParams::FromJson(config));
                }, "http", {"http", "https"});
            }
        };

        TRegistrar R;
    }

}
