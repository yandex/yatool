#include <devtools/libs/universal_fetcher/process_runners/direct/runner.h>
#include <devtools/libs/universal_fetcher/registry/registry.h>

namespace NUniversalFetcher {

    namespace {
        struct TRegistrar {
            inline TRegistrar() {
                auto r = CreateDirectProcessRunner();
                RegisterProcessRunner(r);
            }
        } R;
    }

}
