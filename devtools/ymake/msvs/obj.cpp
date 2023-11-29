#include "obj.h"

#include <devtools/ymake/common/npath.h>

namespace NYMake {
    namespace NMsvs {
        TString TObj::Path() const {
            if (!IntDir.empty()) {
                return NPath::Join(IntDir, FlatName);
            } else {
                return FlatName;
            }
        }
    }
}

