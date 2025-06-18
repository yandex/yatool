#include <devtools/ya/app_config/lib/config.h>

namespace NYa::NConfig {
    const TString DocumentationUrl = "https://github.com/yandex/yatool/tree/main/build/docs";
    const TString SupportUrl = "https://github.com/yandex/yatool/issues";
    const TString Description = "Yet another build tool.\nDocumentation: [[imp]]" + DocumentationUrl + " [[rst]]";
    const bool HasMapping = true;
    const bool InHouse = false;
    const bool HaveOAuthSupport = false;
    const TString JunkRoot = "junk/{username}";
    const TString ExtraConfRoot = "devtools/ya/opensource";
}
