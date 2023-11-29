#pragma once

#include <util/generic/string.h>
#include <util/system/types.h>

namespace NYMake {
    namespace NMsvs {
        size_t ToolSetVersion(size_t vsVersion);
        TString PlatformToolSet(size_t version);
    }
}
