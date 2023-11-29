#pragma once

#include "devtools/local_cache/ac/proto/ac.pb.h"

#include "devtools/local_cache/common/dbbe-running-procs/running_handler.h"

#include <library/cpp/logger/log.h>

#include <util/generic/flags.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/thread/pool.h>

namespace NACCachePrivate {
    class TRunningQueriesStmts;
}

namespace NACCache {
    class TCASManager;
    class TIntegrityHandler;
    using TRunningProcsHandler = ::NCachesPrivate::TRunningProcsHandler<TIntegrityHandler, NACCachePrivate::TRunningQueriesStmts>;

    void DefaultErrorHandler(TLog& log, const std::exception&);

    using TCriticalErrorHandler = NCachesPrivate::TCriticalErrorHandler;

    struct TDBBEReturn {
        EOptim CopyMode = NACCache::Copy;
        bool Success = false;
    };

    /// Main class responsible for DB updates from clients.
    /// DB integrity relies on this class .
    ///
    /// Derived from NToolsCache::TIntegrityHandler with DB handlers replaced.
    class TIntegrityHandler : TNonCopyable {
    public:
        TIntegrityHandler(THolder<NSQLite::TSQLiteDB>&& db, bool noBlobIO, ssize_t diskLimit, int maxQueueSize, TStringBuf casStoreDir, const TCriticalErrorHandler& handler, bool casLogging, TLog& log, bool recreate);
        ~TIntegrityHandler();

        void SetRunningProcHandler(TRunningProcsHandler* runningHandler);

        void SetMasterMode(bool isMaster);
        bool GetMasterMode() const noexcept;

        TDBBEReturn PutUid(const NACCache::TPutUid&);
        TDBBEReturn GetUid(const NACCache::TGetUid&);
        TDBBEReturn RemoveUid(const NACCache::TRemoveUid&);
        TDBBEReturn HasUid(const NACCache::THasUid&);

        bool ForceGC(const TForceGC&);

        void Initialize();
        void Finalize() noexcept;
        void Flush();
        void WakeUpGC();
        size_t GetWorkQueueSizeEstimation() const noexcept;
        /// Returns true if GC_ handler is quiet and no request appeared for some time.
        bool IsQuiescent(int quiescenceTime) const noexcept;
        /// Get task's statistics.
        void GetTaskStats(const NUserService::TPeer&, TTaskStatus*);
        /// Analyze disk usage
        void AnalyzeDU(TDiskUsageSummary*);
        /// Synchronous GC.
        void SynchronousGC(const NACCache::TSyncronousGC& config);

        bool PutDeps(const NACCache::TNodeDependencies&);

    public:
        /// Disk space consumed so far by blobs.
        static TAtomic TotalFSSize;
        /// Apparent disk space consumed so far by blobs.
        static TAtomic TotalSize;
        /// Disk space consumed so far by DB.
        static TAtomic TotalDBSize;
        /// Total # of blobs managed.
        static TAtomic TotalBlobs;
        /// Total # of ac managed.
        static TAtomic TotalACs;
        /// Total # of processes being polled.
        static TAtomic TotalProcs;
        /// Last access count known so far.
        /// Consistency is not strictly enforced between processes.
        /// Should be used only in GC heuristic.
        static TAtomic LastAccessCnt;
        /// MilliSeconds of the last access.
        static TAtomic LastAccessTime;

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
    class TACCacheDBBE {
    public:
        TACCacheDBBE(TStringBuf dbPath, bool noBlobIO, bool enableForeignKey, ssize_t limit, TLog& log, const TCriticalErrorHandler& handler, TStringBuf casStoreDir, int procMaxSize, int gcMaxSize, int quiescenceTime, bool casLogging, bool recreate);
        ~TACCacheDBBE();

        TDBBEReturn PutUid(const NACCache::TPutUid&);
        TDBBEReturn GetUid(const NACCache::TGetUid&);
        TDBBEReturn RemoveUid(const NACCache::TRemoveUid&);
        TDBBEReturn HasUid(const NACCache::THasUid&);

        bool ForceGC(const TForceGC&);

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

        /// Returns true if GC_ handler is quiet and no request appeared for some time.
        bool IsQuiescent() const noexcept;

        /// Get task's statistics.
        void GetTaskStats(const NUserService::TPeer&, TTaskStatus*);
        /// Analyze disk usage
        void AnalyzeDU(TDiskUsageSummary*);
        /// Synchronous GC.
        void SynchronousGC(const NACCache::TSyncronousGC& config);

        /// Put graph information to db
        bool PutDeps(const NACCache::TNodeDependencies&);

        void ReleaseAll(const NUserService::TPeer&);

    private:
        class TDB;
        /// Keep order of initialization. See ctor.
        /// @{
        THolder<TDB> InserterHandlerDB_;
        THolder<TDB> RunningHandlerDB_;
        TIntegrityHandler Inserter_;
        TRunningProcsHandler Runnning_;
        /// @}
        TLog& Log_;
        EPollersConf PollerConf_;
        /// Quiescence time in milliseconds.
        int QuiescenceTime_;
        bool Initialized_;
    };

    /// Create DB file if needed
    /// \return true if existing DB was recreated
    bool CreateDBIfNeeded(const TString& dbPath, TLog& log, bool recreate);
}
