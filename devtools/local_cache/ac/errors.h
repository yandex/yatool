#pragma once

#include <util/generic/yexception.h>

namespace NACCache {
    class TRestoreError: public TIoException {
    public:
        using TIoException::TIoException;
    };

    /// Stored blob is damaged: unlike a restore error, the entry must go.
    class TCorruptedBlobError: public TIoException {
    public:
        using TIoException::TIoException;
    };
} // namespace NACCache
