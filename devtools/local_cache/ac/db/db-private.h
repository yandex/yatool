#pragma once

#include "db-public.h"
#include "cas.h"

#include "devtools/local_cache/psingleton/systemptr.h"

namespace NACCachePrivate {
    using namespace NCachesPrivate;
    using namespace NSQLite;

    class TCASStmts::TImpl : TNonCopyable, public Utilities<TCASStmts::TImpl, EXCLUSIVE> {
    public:
        using TOuter = TCASStmts;
        UTILITY_WRAPPERS(EXCLUSIVE);

        using TListOut = TOuter::TListOut;

        TImpl(NSQLite::TSQLiteDB& db, bool nested);

        // TODO: implement DataRemoved
        TReturn PutBlob(const NACCache::TFsBlobProcessor::TParams& params, NACCache::TFsBlobProcessor& processor, i32 refCountAdj, TLog& log);
        TReturn GetBlob(NACCache::TFsBlobProcessor& processor, TLog& log);

        i64 GetNextChunk(TListOut& out, i64 StartingRowid);

    private:
        /// Bunch of statements to process tables.
        THolder<TSQLiteStatement> Stmts_[Last + 1];
        /// If true then no transactions.
        bool Nested_;
    };

    // Helper class for TACStmts::TImpl
    class TInsertRunningStmts: public NCachesPrivate::TInsertRunningStmts {
    public:
        TInsertRunningStmts(NSQLite::TSQLiteDB& db);
    };

    class TACStmts::TImpl : TNonCopyable, public Utilities<TACStmts::TImpl, EXCLUSIVE> {
    public:
        using TOuter = TACStmts;
        UTILITY_WRAPPERS(EXCLUSIVE);

        TImpl(NSQLite::TSQLiteDB& db);

        TReturn PutUid(const NACCache::TPutUid&, NACCache::TCASManager&, i64 accessCnt);

        TReturn GetUid(const NACCache::TGetUid&, NACCache::TCASManager&, i64 accessCnt);

        TReturn RemoveUid(const NACCache::TRemoveUid&, NACCache::TCASManager& cas);

        TReturn RemoveUidNested(i64, NACCache::TCASManager& cas, NACCache::TRollbackHandler& casGuard);

        TReturn HasUid(const NACCache::THasUid&, i64 accessCnt);

        bool PutDeps(const NACCache::TNodeDependencies& deps);

    private:
        bool SetReturnValue(NACCache::TOrigin& result, i64 rowid);
        bool SetReturnValue(NACCache::TOrigin& result, const TString& uidStr);
        void UpdateGCInformation(i64 acRowid, i64 accessCnt, bool existing, bool resultNode);
        void DeleteAC(i64 acRowid);

        i64 UpdateRequestCountInDBForLock(const NUserService::TPeer& peer, i64 acRowid, i64 refCount);
        i64 UpdateRequestCountInDBForUnlock(const NUserService::TPeer& peer, i64 acRowid, i64 refCount);

        std::tuple<ssize_t, ssize_t, i32> RemoveBlobs(i64 acRowid, NACCache::TCASManager& cas, NACCache::TRollbackHandler& casGuard);

        /// Bunch of statements to process tables.
        THolder<TSQLiteStatement> Stmts_[Last + 1];
        TInsertRunningStmts ProcInserter_;
    };

    class TGcQueriesStmts::TImpl : TNonCopyable, public Utilities<TGcQueriesStmts::TImpl, DEFERRED> {
    public:
        using TOuter = TGcQueriesStmts;
        UTILITY_WRAPPERS(DEFERRED);

        TImpl(TSQLiteDB& db);

        void CleanSomething(NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas);

        void UpdateAcsRefCounts(NACCache::TCancelCallback* callback);

        void SynchronousGC(const NACCache::TSyncronousGC& config, NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas);

        using Utilities<TGcQueriesStmts::TImpl, DEFERRED>::Get;
        using Utilities<TGcQueriesStmts::TImpl, DEFERRED>::GetColumnInt64;

    private:
        // Iterator for CleanSomething
        struct TAsyncRemovalIterator;
        // Same as TAsyncRemovalIterator, but is not preempted by other requests.
        struct TSyncTotalSizeRemovalIterator;
        struct TSyncOldItemsRemovalIterator;
        struct TSyncBigItemsRemovalIterator;

        template <typename Iterator>
        void CleanSomethingWithIter(TACStmts& eraser, NACCache::TCASManager& cas, Iterator& iter);

        THolder<TSQLiteStatement> Stmts_[Last + 1];

        void UpdateStats(const TACStmts::TReturn& out, NACCache::TCancelCallback* callback);
    };

    class TStatQueriesStmts::TImpl : TNonCopyable, public Utilities<TStatQueriesStmts::TImpl, DEFERRED> {
    public:
        using TOuter = TStatQueriesStmts;
        UTILITY_WRAPPERS(DEFERRED);

        TImpl(TSQLiteDB& db);

        NACCache::TStatus GetStatistics();

        int GetLastAccessNumber();

        void GetTaskStats(const NUserService::TPeer& peer, NACCache::TTaskStatus* out);

        void GetTaskStats(i64 taskRef, NACCache::TTaskStatus* out);

        void AnalyzeDU(NACCache::TDiskUsageSummary* out);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];

        i64 PageSize_ = 0;
    };

    // Dummy class.
    class TAcsUpdaterOnTaskRemoval {
    public:
        TAcsUpdaterOnTaskRemoval(TSQLiteDB&) {
        }

        // Unused for incremental cleanup.
        void UpdateResourceOnDeadTask(i64) {
        }

        // Too expensive to compute for logs only
        void ComputeStat(i64) {
        }

        void CheckSqlStatements() const {
        }

        // Dummy statistic
        bool GetStat() const {
            return true;
        }
    };

    struct TACCacheDBSingleton {
        TRWMutex DBMutex;
    };

    TRWMutex& GetACDBMutex();
}
