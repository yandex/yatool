#pragma once

#include "devtools/local_cache/toolscache/proto/tools.pb.h"

#include "devtools/local_cache/common/dbbe-running-procs/running_handler.h"

#include <library/cpp/logger/log.h>

#include <util/generic/flags.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/thread/pool.h>

namespace NToolsCachePrivate {
    class TRunningQueriesStmts;
}

namespace NToolsCache {
    class TGCAndFSHandler;
    using TRunningProcsHandler = ::NCachesPrivate::TRunningProcsHandler<TGCAndFSHandler, NToolsCachePrivate::TRunningQueriesStmts>;
    class TIntegrityHandler;

    void DefaultErrorHandler(TLog& log, const std::exception&);

    using TCriticalErrorHandler = NCachesPrivate::TCriticalErrorHandler;

    /// Main class responsible for DB updates from clients.
    /// DB integrity relies on this class .
    ///
    /// SQLite locks tables, so there is single instance of this class.
    ///
    /// ForceGC relies that InsertResource are performed using single instance.
    ///
    /// Interface to TToolCacheServer
    /// Externally synchronized.
    class TIntegrityHandler : TNonCopyable {
    public:
        TIntegrityHandler(THolder<NSQLite::TSQLiteDB>&& db, TGCAndFSHandler& fsHandler, TRunningProcsHandler& runningHandler, IThreadPool& pool, TLog& log);
        ~TIntegrityHandler();

        void SetMasterMode(bool isMaster);

        bool GetMasterMode() const noexcept;

        /// Insert new resource and its user for tracking.
        void InsertResource(const TResourceUsed&);
        /// Insert new service and its user for tracking, returns the newest service for the same name
        void InsertService(const TServiceStarted&, TServiceInfo* out);
        /// Independent GC in emergency.
        bool ForceGC(const TForceGC&);
        /// Lock resource forever till UnlockSBResource call.
        void LockResource(const TSBResource&);
        /// Unlock resource.
        void UnlockSBResource(const TSBResource&);
        /// Unlock all resources marked with special == 0
        void UnlockAllResources(const NUserService::TPeer& peer);

    public:
        /// Disk space consumed so far by resources.
        static TAtomic TotalSize;
        /// Disk space consumed so far by special resources.
        static TAtomic TotalSizeLocked;
        /// Disk space consumed so far by DB.
        static TAtomic TotalDBSize;
        /// Total # of tools managed.
        static TAtomic TotalTools;
        /// Total # of processes being polled.
        static TAtomic TotalProcs;
        /// # of resource with unknown size.
        static TAtomic UnknownCount;
        /// Last access count known so far.
        /// Consistency is not strictly enforced between processes.
        /// Should be used only in GC heuristic.
        static TAtomic LastAccessCnt;
        /// MilliSeconds of the last access.
        static TAtomic LastAccessTime;

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    /// Started from main process (TIntegrityHandler) responsible for insertion to DB.
    /// Performs following groups of tasks:
    /// 1. FS-related. Compute size and update DB, updates related stats in TIntegrityHandler.
    /// 2. GC-related.
    class TGCAndFSHandler : TNonCopyable {
        template <typename T>
        friend void Out(IOutputStream&, const T&);

    public:
        TGCAndFSHandler(THolder<NSQLite::TSQLiteDB>&& db, ssize_t limit, IThreadPool& pool, TLog& log, const TCriticalErrorHandler& handler, int maxSize);
        ~TGCAndFSHandler();

        void Initialize();
        void Finalize() noexcept;
        void Flush();
        void AddResource(const TSBResource& res, i64 rowid);
        void WakeUpGC();
        void ForceGC(i64 targetSize);
        size_t GetWorkQueueSizeEstimation() const noexcept;
        void SetMasterMode(bool isMaster);
        bool GetMasterMode() const noexcept;
        /// Get task's statistics.
        void GetTaskStats(const NUserService::TPeer&, TTaskStatus*);

    public:
        /// # of times request was blocked.
        static TAtomic DBLocked;

        /// # of times capacity was insufficient.
        static TAtomic CapacityIssues;

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    enum EPollersFlags : int {
        PollGCAndFS = 1,
        PollProcs = 2,
        PollAll = PollGCAndFS | PollProcs
    };

    Y_DECLARE_FLAGS(EPollersConf, EPollersFlags);
    Y_DECLARE_OPERATORS_FOR_FLAGS(EPollersConf);

    // Wrapper for the classes above to keep fix correct initialization order.
    class TToolsCacheDBBE {
    public:
        TToolsCacheDBBE(TStringBuf dbPath, ssize_t limit, IThreadPool& pool, TLog& log, const TCriticalErrorHandler& handler, int procMaxSize, int gcMaxSize, int quiescenceTime);
        ~TToolsCacheDBBE();

        // @{
        /// Insert new resource and its user for tracking.
        void InsertResource(const TResourceUsed&);
        /// Insert new service and its user for tracking, returns the newest service for the same name
        void InsertService(const TServiceStarted&, TServiceInfo* out);
        /// Independent GC in emergency.
        bool ForceGC(const TForceGC&);
        /// Lock resource forever till UnlockSBResource call.
        void LockResource(const TSBResource&);
        /// Unlock resource.
        void UnlockSBResource(const TSBResource&);
        /// Unlock all resources marked with special == 0
        void UnlockAllResources(const NUserService::TPeer& peer);
        // @}

        void SetMasterMode(bool isMaster);
        bool GetMasterMode() const noexcept;

        void Initialize(EPollersConf conf = PollAll);
        void Finalize() noexcept;

        /// Flush work queues, in-flight items may be added immediately.
        /// Accurate Flush only after Finalize().
        void Flush();

        /// Estimated total size of working queues.
        /// Accurate number is available after Finalize().
        size_t GetWorkQueueSizeEstimation() const noexcept;

        /// Return statistics known so far.
        void GetStats(TStatus* out) const noexcept;

        /// Returns true if GC_ handler is quiet and not request appeared for some time.
        bool IsQuiescent() const noexcept;

        /// Get task's statistics.
        void GetTaskStats(const NUserService::TPeer&, TTaskStatus*);

    private:
        class TDB;
        THolder<TDB> GCHandlerDB_;
        THolder<TDB> RunningHandlerDB_;
        THolder<TDB> InserterHandlerDB_;
        /// Keep order of initialization. See ctor.
        /// @{
        TGCAndFSHandler GC_;
        TRunningProcsHandler Runnning_;
        TIntegrityHandler Inserter_;
        /// @}
        TLog& Log_;
        EPollersConf PollerConf_;
        /// Quiescence time in milliseconds.
        int QuiescenceTime_;
        bool Initialized_;
    };

    /// Create DB file if needed and/or return the version of the previous instance.
    int CreateDBIfNeeded(const TString& dbPath, TLog& log);
}

namespace NToolsCache {
    inline bool operator==(const TSBResource& a, const TSBResource& b) {
        return a.GetSBId() == b.GetSBId() && a.GetPath() == b.GetPath();
    }

    inline bool operator!=(const TSBResource& a, const TSBResource& b) {
        return !operator==(a, b);
    }
}

template <>
struct THash<NToolsCache::TSBResource> {
    using TType = NToolsCache::TSBResource;
    inline size_t operator()(const TType& p) const {
        return THash<TString>()(p.GetSBId()) + THash<TString>()(p.GetPath());
    }
};
