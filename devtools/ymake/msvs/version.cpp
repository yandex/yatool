#include "version.h"

#include "error.h"

#include <util/generic/yexception.h>
#include <util/string/cast.h>

namespace NYMake {
    namespace NMsvs {
        size_t ToolSetVersion(size_t vsVersion) {
            switch (vsVersion) {
                case 2017:
                    return 141;
                case 2019:
                    return 142;
                default:
                    ythrow TMsvsError() << "Unsupported Visual Studio version: " << vsVersion << Endl;
            }
        }

        TString PlatformToolSet(size_t version) {
            TStringStream versionStr;
            versionStr << 'v' << ::ToString(version);
            return versionStr.Str();
        }
    }
}
