#pragma once

#include <util/generic/string.h>
#include <util/folder/fts.h>

namespace NFsPrivate {
    size_t StatSize(stat_struct* statPtr);
    void LStat(const TString& fileName, stat_struct* statPtr);
    void Chmod(const TString& fileName, int pmode);
    bool IsLink(ui64 mode);
}
