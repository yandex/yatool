#include "std_helpers.h"

#include <library/cpp/testing/common/env.h>

namespace NYexport {

    fs::path ArcadiaSourceRootPath() {
        return ArcadiaSourceRoot().c_str();
    }

}
