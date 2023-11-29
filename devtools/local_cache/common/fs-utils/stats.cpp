#include "stats.h"

#include <util/generic/yexception.h>

#if defined(_win_)
#include <io.h>
#include <util/folder/lstat_win.h>
#elif defined(_unix_)
#include <sys/stat.h>
#endif

size_t NFsPrivate::StatSize(stat_struct* statPtr) {
#if defined(_win_)
    return statPtr->st_size;
#else
    // Over-estimation for sparse files, but some meaningful value for Arc/FUSE.
    return Max(statPtr->st_blocks * 512, statPtr->st_size);
#endif
}

void NFsPrivate::LStat(const TString& fileName, stat_struct* statPtr) {
    Zero(*statPtr);
#if defined(_win_)
    int r = ::_stat64(fileName.c_str(), statPtr);
#elif defined(_unix_)
    int r = ::lstat(fileName.c_str(), statPtr);
#endif
    if (r == -1) {
        ythrow TIoException() << "failed to stat " << fileName;
    }
}

void NFsPrivate::Chmod(const TString& fileName, int pmode) {
#if defined(_win_)
    int r = ::_chmod(fileName.c_str(), pmode);
#elif defined(_unix_)
    int r = ::chmod(fileName.c_str(), pmode);
#endif
    if (r == -1) {
        ythrow TIoException() << "failed to chmod " << fileName;
    }
}

bool NFsPrivate::IsLink(ui64 mode) {
#if defined(_win_)
    return (mode & _S_IFLNK) == _S_IFLNK;
#elif defined(_unix_)
    return S_ISLNK(mode);
#else
#error "Unknown platform"
#endif
}
