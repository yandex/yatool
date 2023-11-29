#pragma once

#include "db-private.h"

namespace {
    static const char* RUNNING_NAMES[] = {
        "@Ctime",
        "@Pid",
        "@ProcRef",
        "@TaskCtime",
        "@TaskRef"};

}

/// TRunningQueriesStmts implementation.
namespace NCachesPrivate {
    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TImpl::TImpl(TSQLiteDB& db, TRWMutex& dbMutex)
        : TBase(db, 1, dbMutex)
        , UpdateResource_(db)
    {
        const char* resources[] = {Incremental == NonIncrementalTaskErasure ? "running" : "running_inc"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &RUNNING_NAMES, db);
        Y_ABORT_UNLESS(this->Get(DropDeadProcess).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(this->Get(DropProcessLink).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(this->Get(DropDeadTask).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(this->Get(GetRunningId).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(this->Get(MarkDeadTask).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(this->Get(SelectRunning).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(this->Get(StillRunningTask).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(this->Get(UsingTasks).BoundParameterCount() == 1);
        UpdateResource_.CheckSqlStatements();
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    typename TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TVecOut TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TImpl::GetRunningProcs(time_t since) {
        this->Get(SelectRunning).Bind("@TaskCtime", since);

        TVecOut out;
        TBase::WrapInTransactionVoid([this, &out]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            out.clear();
            while (this->Get(SelectRunning).Step()) {
                TProcessUID p(this->GetColumnInt64(SelectRunning, 1, "ProcPid"),
                              this->GetColumnInt64(SelectRunning, 2, "TaskCtime"));
                out.emplace_back(
                    static_cast<i64>(this->GetColumnInt64(SelectRunning, 0, "ProcRef")),
                    static_cast<i64>(this->GetColumnInt64(SelectRunning, 3, "ExpectedLife")),
                    p);
            }
        });
        return out;
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    Stat TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TImpl::DropDeadProc(const TMaybe<i64>& rowid, const TProcessUID& pid) {
        return TBase::WrapInTransaction([this, &rowid, &pid]() -> Stat {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            i64 id = -1;
            if (rowid) {
                id = rowid.GetRef();
            } else {
                if (!this->Get(GetRunningId).Bind("@Pid", (i64)pid.GetPid()).Bind("@Ctime", pid.GetStartTime()).Step()) {
                    return UpdateResource_.GetStat();
                }
                id = this->GetColumnInt64(GetRunningId, 0, "Rowid");
            }

            // Obtain single task for this process
            i64 taskRef = -1;
            if (this->Get(UsingTasks).Bind("@ProcRef", id).Step()) { // There should be corresponding task
                taskRef = this->GetColumnInt64(UsingTasks, 0, "TaskRef");
            } else {
                Y_ABORT_UNLESS(0);
            }
            Y_ABORT_UNLESS(!this->Get(UsingTasks).Step()); // There should be single corresponding task

            // Check if there is other processes for given task.
            bool hasAliveProc = this->Get(StillRunningTask).Bind("@TaskRef", taskRef).Bind("@ProcRef", id).Step();

            UpdateResource_.ComputeStat(taskRef);

            // Perform destructive operations.
            this->Get(DropProcessLink).Bind("@ProcRef", id).Execute();
            this->Get(DropDeadProcess).Bind("@ProcRef", id).Execute();
            if (hasAliveProc) {
                return UpdateResource_.GetStat();
            }

            if (Incremental == IncrementalTaskErasure) {
                this->Get(MarkDeadTask).Bind("@TaskRef", taskRef).Execute();
            } else {
                UpdateResource_.UpdateResourceOnDeadTask(taskRef);
                this->Get(DropDeadTask).Bind("@TaskRef", taskRef).Execute();
            }
            return UpdateResource_.GetStat();
        });
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TRunningQueriesStmts(TSQLiteDB& db, TRWMutex& dbMutex) {
        Ref_.Reset(new TImpl(db, dbMutex));
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::~TRunningQueriesStmts() {
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    typename TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::TVecOut TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::GetRunningProcs(time_t since) {
        return Ref_->GetRunningProcs(since);
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    Stat TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::DropDeadProc(i64 id) {
        return Ref_->DropDeadProc(MakeMaybe(id), TProcessUID());
    }

    template <typename ResourceUpdater, typename Stat, EIncrementalTaskErasure Incremental>
    Stat TRunningQueriesStmts<ResourceUpdater, Stat, Incremental>::DropDeadProc(const TProcessUID& pid) {
        return Ref_->DropDeadProc(TMaybe<i64>(), pid);
    }
}
