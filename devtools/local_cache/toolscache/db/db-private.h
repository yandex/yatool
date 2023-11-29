#pragma once

#include "db-public.h"

namespace NToolsCachePrivate {
    using namespace NCachesPrivate;
    using namespace NSQLite;

    // Helper class for TNotifyStmts::TImpl
    class TInsertRunningStmts: public NCachesPrivate::TInsertRunningStmts {
    public:
        TInsertRunningStmts(NSQLite::TSQLiteDB& db);
    };

    class TNotifyStmts::TImpl : TNonCopyable, public Utilities<TNotifyStmts::TImpl, EXCLUSIVE> {
    public:
        using TOuter = TNotifyStmts;
        UTILITY_WRAPPERS(EXCLUSIVE);

        using TVecOut = TOuter::TVecOut;

    public:
        TImpl(TSQLiteDB& db);

        std::tuple<i64, i64, size_t> UpdateResourceUsage(i64 accessCnt, const NToolsCache::TResourceUsed&, bool refill, TVecOut& emergencyGC);

        std::tuple<i64, i64, size_t> UpdateServiceUsage(i64 accessCnt, const NToolsCache::TServiceStarted&, bool refill, TVecOut& emergencyGC, NToolsCache::TServiceInfo*);

    private:
        /// Bind values in statement for update.
        void BindStmts(i64 accessCnt, const NToolsCache::TSBResource&);
        /// Update tables responsible for integrity.
        /// Returns id from 'tools' and 'running'
        std::tuple<i64, i64, size_t> UpdateTablesForIntegrity(EToolsSpecial special, const NUserService::TPeer&);
        /// Service-specific part of transaction responsible for integrity.
        void UpdateTablesForServices(i64 toolRowid, const NToolsCache::TServiceStarted&, NToolsCache::TServiceInfo*);
        /// Update tables responsible for gc.
        void UpdateTablesForGC(i64 toolRowid, const NToolsCache::TResourceUsed&);
        void CollectEmergencyGC(TVecOut& emergencyGC);

    private:
        /// Bunch of statements to process tables.
        THolder<TSQLiteStatement> Stmts_[Last + 1];
        TInsertRunningStmts ProcInserter_;
    };

    class TLockResourceStmts::TImpl : TNonCopyable, public Utilities<TLockResourceStmts::TImpl, EXCLUSIVE> {
    public:
        using TOuter = TLockResourceStmts;
        UTILITY_WRAPPERS(EXCLUSIVE);

    public:
        TImpl(TSQLiteDB& db);

        void LockResource(const NToolsCache::TSBResource&);
        void UnlockSBResource(const NToolsCache::TSBResource&);
        void UnlockAllResources();

    private:
        /// Bunch of statements to process tables.
        THolder<TSQLiteStatement> Stmts_[Last + 1];
    };

    class TFsModStmts::TImpl : TNonCopyable, public Utilities<TFsModStmts::TImpl, EXCLUSIVE> {
    public:
        using TOuter = TFsModStmts;
        UTILITY_WRAPPERS(EXCLUSIVE);

        TImpl(TSQLiteDB& db, IThreadPool& pool);

        /// Unsafe delete directory.
        NThreading::TFuture<NToolsCache::TSBResource> UnsafeDeleteDir(const NToolsCache::TSBResource&);

        /// Safe delete directory.
        /// Needs rowid. Returns parameters of removed resource.
        std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> SafeDeleteDir(i64, EKeepDir mode);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
        IThreadPool& Pool_;
    };

    class TFsFillerStmts::TImpl : TNonCopyable, public Utilities<TFsFillerStmts::TImpl, DEFERRED> {
    public:
        using TOuter = TFsFillerStmts;
        UTILITY_WRAPPERS(DEFERRED);

        /// pair.first is a rowid for SetComputedSize
        using TVecOut = TOuter::TVecOut;

        TImpl(TSQLiteDB& db);

        /// Get a chunk to compute disk usage.
        /// (path, sbid)
        /// \a all means not only tracked by gc
        TVecOut GetDirsToComputeSize(bool all);
        /// Update disk usage: rowid, disk_size.
        /// (rowid, size)
        void SetComputedSize(i64 rowid, size_t diskSize);

        /// Insert discovered directory EToolsSpecial::Stale
        void InsertDiscoveredDirs(TFsPath path, const TVector<i64>& discovered);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
    };

    class TGcQueriesStmts::TImpl : TNonCopyable, public Utilities<TGcQueriesStmts::TImpl, DEFERRED> {
    public:
        using TOuter = TGcQueriesStmts;
        UTILITY_WRAPPERS(DEFERRED);

        TImpl(TSQLiteDB& db);

        TMaybe<std::pair<i64, size_t>> GetSomethingToClean();
        TMaybe<i64> GetStaleToClean();

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
    };

    class TStatQueriesStmts::TImpl : TNonCopyable, public Utilities<TStatQueriesStmts::TImpl, DEFERRED> {
    public:
        using TOuter = TStatQueriesStmts;
        UTILITY_WRAPPERS(DEFERRED);

        TImpl(TSQLiteDB& db);

        NToolsCache::TStatus GetStatistics();

        int GetLastAccessNumber();

        void GetTaskStats(const NUserService::TPeer& peer, NToolsCache::TTaskStatus* out);

        void GetTaskStats(i64 taskRef, NToolsCache::TTaskStatus* out);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];

        i64 PageSize_ = 0;
    };

    class TToolsUpdaterOnTaskRemoval: public Wrappers<TToolsUpdaterOnTaskRemoval> {
    public:
        using TOuter = TRunningQueriesStmts;
        using EStmtName = TRunningQueriesStmts::EStmtName;
        SQL_STMTS_UTILITIES();

        TToolsUpdaterOnTaskRemoval(TSQLiteDB& db);

        void UpdateResourceOnDeadTask(i64 taskRef);
        void ComputeStat(i64 taskRef);
        void CheckSqlStatements() const;
        NToolsCache::TTaskStatus GetStat() const {
            return TotalDiskSize_;
        }

    private:
        THolder<TSQLiteStatement> Stmts_[TOuter::Last + 1];
        TStatQueriesStmts Stats_;

        NToolsCache::TTaskStatus TotalDiskSize_;
    };

    class TServiceVersionQuery::TImpl : TNonCopyable, public Utilities<TServiceVersionQuery::TImpl, DEFERRED> {
    public:
        using TOuter = TServiceVersionQuery;
        UTILITY_WRAPPERS(DEFERRED);

        TImpl(TSQLiteDB& db);

        int GetServiceVersion(const TString& serviceName);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
    };
}
