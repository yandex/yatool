#pragma once

#include <util/generic/yexception.h>

namespace NACCache {
    class TRestoreError: public TIoException {
    public:
        using TIoException::TIoException;
    };
} // namespace NACCache
