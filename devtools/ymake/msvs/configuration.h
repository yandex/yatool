#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

namespace NYMake {
    namespace NMsvs {
        enum EConf {
            C_UNSET,
            C_DEBUG,
            C_RELEASE,
            CONFS
        };

        const TStringBuf& Configuration(EConf conf);
        TString ResolveBuildTypeSpec(const TStringBuf& s, EConf conf);
    }
}
