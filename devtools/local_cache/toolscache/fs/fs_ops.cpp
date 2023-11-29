#include "fs_ops.h"

#include "devtools/local_cache/common/fs-utils/stats.h"

#include <library/cpp/threading/future/async.h>

#include <util/folder/iterator.h>
#include <util/folder/path.h>
#include <util/folder/dirut.h>
#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/stream/file.h>
#include <util/string/split.h>
#include <util/system/file.h>
#include <util/system/fs.h>
#include <util/system/rwlock.h>
#include <util/system/sysstat.h>
#include <util/thread/factory.h>

#include <errno.h>

namespace {
    const char* INSTALLED = "INSTALLED";
    const char* PLATFORM_CACHE = ".platform.cache";

    struct TFSSingleton {
        TRWMutex DBMutex;
    };

    // Guards concurrent removals from different threads.
    static TRWMutex& GetFSMutext() {
        return Singleton<TFSSingleton>()->DBMutex;
    }

    // See devtools/ya/yalibrary/fetcher/__init__.py: _installed().
    bool IsInstalled(const TFsPath& path) {
        TFsPath installGuard(JoinFsPaths(path, INSTALLED));
        if (!installGuard.Exists()) {
            return false;
        }
        int version = 0;
        try {
            if (!TryFromString<int>(TUnbufferedFileInput(TFile(installGuard, RdOnly)).ReadAll(), version) || version < 2) {
                return false;
            }
        } catch (const TFileError& e) {
            if (e.Status() == ENOENT) {
                return false;
            }
            throw;
        }
        return true;
    }

    size_t FsSize(const TFsPath& path) {
        THashSet<ino_t> inodes;
        size_t totalSize = 0;
        if (path.Exists() || path.IsSymlink()) {
            for (auto& f : TDirIterator(path, TDirIterator::TOptions())) {
                if (f.fts_statp->st_nlink > 1) {
                    if (inodes.contains(f.fts_statp->st_ino)) {
                        continue;
                    }

                    inodes.insert(f.fts_statp->st_ino);
                }

                totalSize += NFsPrivate::StatSize(f.fts_statp);
            }
        }
        {
            TFsPath lock(ToString(path) + ".lock");
            if (lock.Exists()) {
                for (auto& f : TDirIterator(lock)) {
                    totalSize += NFsPrivate::StatSize(f.fts_statp);
                }
            }
        }
        return totalSize;
    }

    void RemoveDir(const TString& npath) {
        TFsPath fsNpath(npath);
        if (fsNpath.IsDirectory()) {
            NFs::RemoveRecursive(npath);
        } else if (fsNpath.Exists() || fsNpath.IsSymlink()) {
            NFs::Remove(npath);
        }
    }

    TString RemoveGuards(const TString& npath, const TString& nlock) {
        // See devtools/ya/yalibrary/fetcher/__init__.py: _installed().
        TFsPath installGuard(JoinFsPaths(npath, INSTALLED));
        installGuard.DeleteIfExists();

        // See devtools/ya/yalibrary/fetcher/__init__.py: _install_cache().
        TFsPath cache(JoinFsPaths(npath, PLATFORM_CACHE));
        cache.DeleteIfExists();

        TFsPath lock(nlock);
        lock.DeleteIfExists();

        for (int i = 0; i < 5; ++i) {
            TFsPath candidate(ToString(npath) + "-" + ToString(i));
            if (candidate.Exists()) {
                continue;
            }
            if (!NFs::Rename(npath, candidate)) {
                continue;
            }
            return candidate;
        }

        // Fallback to explicit synchronous removal.
        // Implicit rate-limit if too many removal of the same resource.
        RemoveDir(npath);
        return "";
    }
}

bool NToolsCache::IsInstalled(const NToolsCache::TSBResource& sb) {
    TReadGuard sync(GetFSMutext());

    TFsPath path(JoinFsPaths(sb.GetPath(), sb.GetSBId()));
    return ::IsInstalled(path);
}

bool NToolsCache::CheckResourceDirDeleted(const NToolsCache::TSBResource& sb) {
    TReadGuard sync(GetFSMutext());

    TFsPath path(JoinFsPaths(sb.GetPath(), sb.GetSBId()));
    return !path.Exists() && !path.IsSymlink();
}

size_t NToolsCacheTest::ComputeDU(TString dir) {
    TReadGuard sync(GetFSMutext());

    return ::FsSize(TFsPath(dir));
}

size_t NToolsCache::ComputeSize(const NToolsCache::TSBResource& sb) {
    TReadGuard sync(GetFSMutext());

    TFsPath path(JoinFsPaths(sb.GetPath(), sb.GetSBId()));

    if (path.IsSymlink()) {
        return FsSize(path);
    }

    if (!::IsInstalled(path)) {
        return -1;
    }

    return ::FsSize(path);
}

NThreading::TFuture<NToolsCache::TSBResource> NToolsCache::RemoveAtomically(const NToolsCache::TSBResource& sb, bool synchronous, IThreadPool& pool) {
    TWriteGuard sync(GetFSMutext());

    TString npath(JoinFsPaths(sb.GetPath(), sb.GetSBId()));
    TFsPath path(npath);

    {
        if (path.IsSymlink()) {
            path.DeleteIfExists();
            TFsPath(ToString(npath) + ".lock").DeleteIfExists();
            return NThreading::MakeFuture(sb);
        }

        if (!path.Exists()) {
            return NThreading::MakeFuture(sb);
        }
    }

    auto renamed = ::RemoveGuards(npath, npath + ".lock");
    auto asyncRemoval = [renamed, sb]() -> NToolsCache::TSBResource {
        // No need to acquire lock here, because there is no race for renamed directory.
        // Removal of empty directory is atomic.
        ::RemoveDir(renamed);
        return sb;
    };

    if (synchronous) {
        return NThreading::MakeFuture(asyncRemoval());
    }

    return NThreading::Async(asyncRemoval, pool);
}

std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> NToolsCache::RemoveAtomicallyIfBroken(const NToolsCache::TSBResource& sb, bool synchronous, IThreadPool& pool) {
    TString npath(JoinFsPaths(sb.GetPath(), sb.GetSBId()));
    TFsPath path(npath);

    if (!path.Exists()) {
        // As resource was removed
        return std::make_pair(NThreading::MakeFuture(sb), true);
    }

    if (path.IsSymlink()) {
        auto target = path.ReadLink();
        if (TFsPath(JoinFsPaths(target, PLATFORM_CACHE)).Exists()) {
            return std::make_pair(NThreading::MakeFuture(sb), false);
        }
    }

    {
        // Avoid recursive lock acquisition.
        TReadGuard sync(GetFSMutext());
        if (::IsInstalled(path)) {
            return std::make_pair(NThreading::MakeFuture(sb), false);
        }
    }

    return std::make_pair(NToolsCache::RemoveAtomically(sb, synchronous, pool), true);
}

NThreading::TFuture<TVector<TString>> NToolsCache::GetListing(const TString& topDir, IThreadPool& pool) {
    auto asyncListing = [topDir]() -> TVector<TString> {
        TVector<TString> out;
        try {
            TFsPath(topDir).ListNames(out);
        } catch (const TIoException& e) {
        }
        return out;
    };
    return NThreading::Async(asyncListing, pool);
}

NToolsCache::EFileKind NToolsCache::ClassifyPath(const TString& baseName) {
    auto chunks = StringSplitter(baseName).Split('.').Limit(3).ToList<TString>();
    ui64 sbId;
    if (chunks.size() > 2 || !TryFromString(chunks[0], sbId)) {
        return Garbage;
    }

    if (chunks.size() == 1) {
        return Resource;
    }

    if (chunks.size() == 2 && chunks[1] == "lock") {
        return Lock;
    }

    return Garbage;
}
