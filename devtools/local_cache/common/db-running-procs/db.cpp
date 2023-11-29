#include "db-private.h"
#include "db_running_impl.h"

namespace {
    static const char* INSERT_STMTS_NAMES[] = {
        // Expected life-time of proc
        "@ExpectedLife",
        // Result of GetRunningRowid
        "@GetRunningId",
        // Result of GetTaskId
        "@GetTaskId",
        "@Task",
        "@ProcCTime",
        "@ProcPid"};
}

/// TRunningQueriesStmts implementation.
namespace NCachesPrivate {
    TInsertRunningStmts::TImpl::TImpl(TSQLiteDB& db) {
        const char* resources[] = {"insert"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &INSERT_STMTS_NAMES, db);
    }

    std::pair<i64, i64> TInsertRunningStmts::TImpl::TImpl::InsertRunningProc(const NUserService::TPeer& peer) {
        TString strRunningId;
        THolder<TSelf, TStmtResetter<true>> resetter(this);
        auto& proc = peer.GetProc();

        Y_ABORT_UNLESS(Get(InsertToRunning).BoundParameterCount() == 3);
        Get(InsertToRunning).Bind("@ProcPid", (i64)proc.GetPid()).Bind("@ProcCTime", (i64)proc.GetStartTime()).Bind("@ExpectedLife", (i64)proc.GetExpectedLifeTime());

        Y_ABORT_UNLESS(Get(GetRunningId).BoundParameterCount() == 2);
        Get(GetRunningId).Bind("@ProcPid", (i64)proc.GetPid()).Bind("@ProcCTime", (i64)proc.GetStartTime());

        Y_ABORT_UNLESS(Get(InsertToTasks).BoundParameterCount() == 1);
        // @Task is in transaction

        Y_ABORT_UNLESS(Get(GetTaskId).BoundParameterCount() == 1);
        // @Task is in transaction

        Y_ABORT_UNLESS(Get(InsertToRunningTasks).BoundParameterCount() == 2);
        // InsertToRunningTasks needs @GetTaskId
        // InsertToRunningTasks needs @GetRunningId

        Get(InsertToRunning).Execute();
        i64 runningId = GetRowid(GetRunningId);
        strRunningId = ToString(runningId);

        if (peer.GetTaskGSID().empty()) {
            Get(InsertToTasks).Bind("@Task", strRunningId);
            Get(GetTaskId).Bind("@Task", strRunningId);
        } else {
            Get(InsertToTasks).Bind("@Task", peer.GetTaskGSID());
            Get(GetTaskId).Bind("@Task", peer.GetTaskGSID());
        }

        Get(InsertToTasks).Execute();
        i64 taskId = GetRowid(GetTaskId);

        Get(InsertToRunningTasks).Bind("@GetTaskId", taskId).Bind("@GetRunningId", runningId).Execute();
        return std::make_pair(taskId, runningId);
    }

    TInsertRunningStmts::TInsertRunningStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TInsertRunningStmts::~TInsertRunningStmts() {
    }

    std::pair<i64, i64> TInsertRunningStmts::InsertRunningProc(const NUserService::TPeer& peer) {
        return Ref_->InsertRunningProc(peer);
    }
}
