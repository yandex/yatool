#pragma once

#include "db-public.h"

namespace NCachesPrivate {
    using namespace NSQLite;

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    class TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TImpl : TNonCopyable, public Utilities<TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TImpl, DEFERRED> {
    public:
        using TOuter = TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>;
        UTILITY_WRAPPERS(DEFERRED);

        // pair.first is a rowid for DropDeadProc
        using TVecOut = TRunningQueriesStmts::TVecOut;

        TImpl(TSQLiteDB& db, TRWMutex& dbMutex);

        // (rowid, proc)
        TVecOut GetRunningProcs(time_t since);

        Stat DropDeadProc(const TMaybe<i64>& id, const TProcessUID& pid);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
        ResourceUpdater UpdateResource_;
    };

    // Should be wrapped into transaction.
    class TInsertRunningStmts::TImpl : TNonCopyable, public Wrappers<TInsertRunningStmts::TImpl> {
    public:
        using TOuter = TInsertRunningStmts;
        using TSelf = TOuter::TImpl;
        SQL_STMTS_UTILITIES();

        TImpl(TSQLiteDB& db);

        std::pair<i64, i64> InsertRunningProc(const NUserService::TPeer& peer);

    private:
        THolder<TSQLiteStatement> Stmts_[Last + 1];
    };
}
