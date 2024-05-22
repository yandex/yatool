#include <devtools/ya/app_config/lib/config.h>

namespace NYa::NConfig {
    const TString Description = "Yet another build tool.";
    const bool HasMapping = true;
    const bool InHouse = false;
    const bool HaveSandboxFetcher = false;
    const bool HaveOAuthSupport = false;
    const TString JunkRoot = "junk/{username}";
    const TString ExtraConfRoot = "devtools/ya/opensource";
}
