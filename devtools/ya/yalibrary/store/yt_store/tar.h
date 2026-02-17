#pragma once

#include <util/folder/path.h>
#include <util/generic/yexception.h>

namespace NYa::NTar {
    class TTarError : public yexception {
    };

    struct IUntarInput {
        virtual size_t Read(const void** buffer) = 0;
        virtual ~IUntarInput() = default;
    };

    void Tar(IOutputStream& dest, const TFsPath& rootDir, const TVector<TFsPath>& files);
    void Untar(IUntarInput& source, const TFsPath& destPath);
}
