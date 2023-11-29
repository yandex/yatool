#pragma once

#include "module.h"

#include <util/generic/strbuf.h>

namespace NYMake {
    namespace NMsvs {
        class TProject {
        private:
            const TModule& Module;

        public:
            explicit TProject(const TModule& module)
                : Module(module)
            {
            }

            const TStringBuf& ConfigurationType() const;
            const TStringBuf& TargetExtention() const;
        };
    }
}
