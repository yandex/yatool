#include <devtools/libs/universal_fetcher/fetchers/docker_fetcher/docker_fetcher.h>
#include <devtools/libs/universal_fetcher/registry/registry.h>


namespace NUniversalFetcher {

    namespace {
        struct TRegistrar {
            TRegistrar() {
                RegisterFetcher([](const NJson::TJsonValue& json, const TProcessRunnerPtr& runner) -> TFetcherPtr {
                    return CreateDockerFetcher(TDockerFetcherConfig::FromJson(json), runner);
                }, "docker", {"docker"});
            }
        };

        TRegistrar R;
    }

}
