#pragma once

#include "module.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

namespace NYMake {
    namespace NMsvs {

        TString GenGuid(const TStringBuf& s);
        TString GenGuid(const TModuleNode& module);

    }
}
