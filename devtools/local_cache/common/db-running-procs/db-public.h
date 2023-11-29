#pragma once

#include "devtools/local_cache/psingleton/systemptr.h"
#include "devtools/local_cache/common/db-utils/dbi.h"
#include "devtools/local_cache/psingleton/proto/known_service.pb.h"

#include <library/cpp/containers/stack_vector/stack_vec.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/system/rwlock.h>

namespace NCachesPrivate {
    enum EIncrementalTaskErasure {
        NonIncrementalTaskErasure,
        IncrementalTaskErasure
    };

    template <EIncrementalTaskErasure Incremental = NonIncrementalTaskErasure>
    class TCreateProcTablesStmt : TNonCopyable {
    public:
        TCreateProcTablesStmt(NSQLite::TSQLiteDB& db)
            : Tables_(db, TVector<TStringBuf>({TStringBuf(Incremental == NonIncrementalTaskErasure ? "running_db" : "running_db_inc")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Tables_;
    };

    struct TRunningQueriesStmtsEnum {
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            SelectRunning,

            UsingTasks,
            StillRunningTask,
            DropProcessLink,
            DropDeadProcess,
            DropDeadTask,
            MarkDeadTask,
            GetRunningId,
            Last = GetRunningId
        };
        using TEnum = EStmtName;
    };

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental = NonIncrementalTaskErasure>
    class TRunningQueriesStmts : TNonCopyable, public TRunningQueriesStmtsEnum {
    public:
        using TEnum = typename TRunningQueriesStmtsEnum::TEnum;

        // first is a rowid for DropDeadProc
        // second is expected life time
        // Used internally, so TProcessUID instead of NUserService::TProc
        using TVecOut = TStackVec<std::tuple<i64, i64, TProcessUID>, 16>;

        ~TRunningQueriesStmts();

        /// (rowid, proc)
        TVecOut GetRunningProcs(time_t since = 0);

        /// Remove dead process from 'running'.
        Stat DropDeadProc(i64 id);

        /// Remove dead process from 'running'.
        Stat DropDeadProc(const TProcessUID& pid);

    protected:
        TRunningQueriesStmts(NSQLite::TSQLiteDB& db, TRWMutex& dbMutex);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    /// Main interface to process blobs table.
    /// See TCASManager in cas/cas.h
    class TInsertRunningStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            InsertToRunning,
            GetRunningId,
            InsertToTasks,
            GetTaskId,
            InsertToRunningTasks,
            Last = InsertToRunningTasks
        };
        using TEnum = EStmtName;
        ~TInsertRunningStmts();

        // (tasks rowid, running rowid)
        std::pair<i64, i64> InsertRunningProc(const NUserService::TPeer& peer);

    protected:
        /// \arg nested If true then wrap into transaction, otherwise rely on caller
        TInsertRunningStmts(NSQLite::TSQLiteDB& db);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };
}
