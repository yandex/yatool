#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYa {
    struct IYaHandler {
        virtual void Run(const TVector<TStringBuf>& args) = 0;
        virtual bool AllowLogging() const { return true; }
        virtual ~IYaHandler() = default;
    };
}
