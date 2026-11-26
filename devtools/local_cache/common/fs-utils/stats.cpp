#include "stats.h"

#include <util/generic/yexception.h>

#if defined(_win_)
#include <io.h>
#include <util/charset/wide.h>
#include <util/folder/lstat_win.h>
#include <util/system/fs.h>
#include <util/system/fstat.h>
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
    const TFileStat nofollow(fileName, true);
    if (nofollow.IsSymlink()) {
        // The CRT follows targets, which can be absent after a relative link is
        // staged in CAS. Preserve the link's kind and its target's byte length.
        statPtr->st_mode = _S_IFLNK | 0444 | ((nofollow.Mode & _S_IWRITE) ? 0222 : 0);
        statPtr->st_size = NFs::ReadLink(fileName).size();
        statPtr->st_nlink = static_cast<decltype(statPtr->st_nlink)>(nofollow.NLinks);
        statPtr->st_atime = nofollow.ATime;
        statPtr->st_mtime = nofollow.MTime;
        statPtr->st_ctime = nofollow.CTime;
        return;
    }
    // Cache paths are UTF-8; the narrow CRT interprets them in the active code page.
    const auto wideName = UTF8ToWide(fileName);
    static_assert(sizeof(wchar_t) == sizeof(wchar16));
    int r = ::_wstat64(reinterpret_cast<const wchar_t*>(wideName.c_str()), statPtr);
#elif defined(_unix_)
    int r = ::lstat(fileName.c_str(), statPtr);
#endif
    if (r == -1) {
        ythrow TIoException() << "failed to stat " << fileName;
    }
}

void NFsPrivate::Chmod(const TString& fileName, int pmode) {
#if defined(_win_)
    const auto wideName = UTF8ToWide(fileName);
    int r = ::_wchmod(reinterpret_cast<const wchar_t*>(wideName.c_str()), pmode);
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
