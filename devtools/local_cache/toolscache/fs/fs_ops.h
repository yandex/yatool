#pragma once

#include "devtools/local_cache/toolscache/proto/tools.pb.h"

#include <library/cpp/threading/future/future.h>
#include <util/thread/pool.h>
#include <util/generic/string.h>

namespace NToolsCache {
    // devtools/ya/yalibrary/fetcher/__init__.py
    enum EFileKind {
        // os.path.join(SBID)
        Resource,
        // os.path.join(SBID + '.lock')
        Lock,
        // Everything else
        Garbage
    };

    /// Classifies baseName. Only name is checked.
    EFileKind ClassifyPath(const TString& baseName);

    bool IsInstalled(const NToolsCache::TSBResource& sb);

    bool CheckResourceDirDeleted(const NToolsCache::TSBResource& sb);

    /// Compute size, external synchronization for potential removal.
    /// TODO(FUSE/arc are underestimated)
    /// Permission restriction can prevent computation (read-only permission on directory).
    size_t ComputeSize(const NToolsCache::TSBResource& sb);

    /// Remove atomically, external synchronization to avoid race.
    /// Wait for return value to complete asynchronous part.
    ///
    /// TODO: There is a race with old ya-bin using old interface.
    NThreading::TFuture<NToolsCache::TSBResource> RemoveAtomically(const NToolsCache::TSBResource& sb, bool sync, IThreadPool& pool);

    /// Remove atomically if it not installed.
    /// Wait for return value to complete asynchronous part.
    ///
    /// TODO: There is a race with old ya-bin using old interface.
    std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> RemoveAtomicallyIfBroken(const NToolsCache::TSBResource& sb, bool sync, IThreadPool& pool);

    /// Get non-recursive listing, external synchronization to avoid race.
    NThreading::TFuture<TVector<TString>> GetListing(const TString& topDir, IThreadPool& pool);
}

namespace NToolsCacheTest {
    /// Compute size
    /// TODO(FUSE/arc are underestimated)
    /// Permission restriction can prevent computation (read-only permission on directory).
    size_t ComputeDU(TString dir);
}
