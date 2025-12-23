#include "devtools/local_cache/ac/dbbei.h"
#include "devtools/local_cache/ac/db/db-public.h"

#include <devtools/local_cache/common/dbbe-utils/create_db.h>

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/string/cast.h>
#include <util/system/datetime.h>
#include <util/system/fs.h>

namespace NACCache {
    // Need to keep at least one open connection for in-memory DB.
    class TACCacheDBBE::TDB {
    public:
        TDB(TStringBuf dbPath, bool createTables, bool enableForeignKeys)
            : DB_(new NSQLite::TSQLiteDB(ToString(dbPath), SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX))
        {
            if (createTables) {
                NACCachePrivate::TCreateTablesStmt a(*DB_);
            }
            NACCachePrivate::TSetupConnection b(*DB_, enableForeignKeys);
        }

        NSQLite::TSQLiteDB* Release() {
            return DB_.Release();
        }

    private:
        THolder<NSQLite::TSQLiteDB> DB_;
    };

    TACCacheDBBE::TACCacheDBBE(TStringBuf dbPath, bool noBlobIO, bool enableForeignKeys, ssize_t diskLimit, TLog& log, const TCriticalErrorHandler& handler, TStringBuf casStoreDir, int procMaxSize, int gcMaxSize, int quiescenceTime, bool casLogging, bool recreate)
        : InserterHandlerDB_(new TDB(dbPath, true, enableForeignKeys))
        , RunningHandlerDB_(new TDB(dbPath, false, enableForeignKeys))
        , Inserter_(THolder<NSQLite::TSQLiteDB>(InserterHandlerDB_->Release()), noBlobIO, diskLimit, gcMaxSize, casStoreDir, handler, casLogging, log, recreate)
        , Runnning_(THolder<NSQLite::TSQLiteDB>(RunningHandlerDB_->Release()), Inserter_, log, handler, procMaxSize)
        , Log_(log)
        , PollerConf_(PollAll)
        , QuiescenceTime_(quiescenceTime)
        , Initialized_(false)
    {
        Inserter_.SetRunningProcHandler(&Runnning_);
    }

    TACCacheDBBE::~TACCacheDBBE() {
        Finalize();
        Inserter_.SetRunningProcHandler(nullptr);
    }

    TDBBEReturn TACCacheDBBE::PutUid(const NACCache::TPutUid& uidInfo) {
        return Inserter_.PutUid(uidInfo);
    }

    TDBBEReturn TACCacheDBBE::GetUid(const NACCache::TGetUid& uidInfo) {
        return Inserter_.GetUid(uidInfo);
    }

    TDBBEReturn TACCacheDBBE::RemoveUid(const NACCache::TRemoveUid& uidInfo) {
        return Inserter_.RemoveUid(uidInfo);
    }

    TDBBEReturn TACCacheDBBE::HasUid(const NACCache::THasUid& uidInfo) {
        return Inserter_.HasUid(uidInfo);
    }

    bool TACCacheDBBE::ForceGC(const TForceGC& setup) {
        return Inserter_.ForceGC(setup);
    }

    void TACCacheDBBE::SetMasterMode(bool isMaster) {
        Inserter_.SetMasterMode(isMaster);
    }

    bool TACCacheDBBE::GetMasterMode() const noexcept {
        return Inserter_.GetMasterMode();
    }

    void TACCacheDBBE::Initialize(EPollersConf conf) {
        PollerConf_ = conf;
        if (PollerConf_ & PollGCAndFS) {
            AtomicSet(TIntegrityHandler::CapacityIssues, 0);
            AtomicSet(TIntegrityHandler::DBLocked, 0);
            Inserter_.Initialize();
        }
        if (PollerConf_ & PollProcs) {
            AtomicSet(TRunningProcsHandler::CapacityIssues, 0);
            AtomicSet(TRunningProcsHandler::DBLocked, 0);
            Runnning_.Initialize();
        }
        Initialized_ = true;
    }

    void TACCacheDBBE::Finalize() noexcept {
        if (!Initialized_) {
            return;
        }
        Runnning_.Finalize();
        Inserter_.Finalize();
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Restarted transaction procs=" << TRunningProcsHandler::DBLocked << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Restarted transaction gc=" << TIntegrityHandler::DBLocked << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Queue capacity issues procs=" << TRunningProcsHandler::CapacityIssues << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Queue capacity issues gc=" << TIntegrityHandler::CapacityIssues << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Queue size procs=" << Runnning_.GetWorkQueueSizeEstimation() << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACBE]") << "Queue size gc=" << Inserter_.GetWorkQueueSizeEstimation() << Endl;
        PollerConf_ = PollAll;
        Initialized_ = false;
    }

    void TACCacheDBBE::Flush() {
        Runnning_.Flush();
        Inserter_.Flush();
    }

    size_t TACCacheDBBE::GetWorkQueueSizeEstimation() const noexcept {
        return Runnning_.GetWorkQueueSizeEstimation() + Inserter_.GetWorkQueueSizeEstimation();
    }

    void TACCacheDBBE::GetStats(TStatus* out) const noexcept {
        out->SetTotalFSSize(AtomicGet(TIntegrityHandler::TotalFSSize));
        out->SetTotalSize(AtomicGet(TIntegrityHandler::TotalSize));
        out->SetMaster(Inserter_.GetMasterMode());
        out->SetTotalDBSize(AtomicGet(TIntegrityHandler::TotalDBSize));
        out->SetBlobCount(AtomicGet(TIntegrityHandler::TotalBlobs));
        out->SetUidCount(AtomicGet(TIntegrityHandler::TotalACs));
        out->SetProcessesCount(AtomicGet(TIntegrityHandler::TotalProcs));
    }

    void TACCacheDBBE::GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
        Inserter_.GetTaskStats(peer, out);
    }

    void TACCacheDBBE::AnalyzeDU(TDiskUsageSummary* out) {
        Inserter_.AnalyzeDU(out);
    }

    void TACCacheDBBE::SynchronousGC(const NACCache::TSyncronousGC& config) {
        Inserter_.SynchronousGC(config);
    }

    bool TACCacheDBBE::PutDeps(const NACCache::TNodeDependencies& deps) {
        return Inserter_.PutDeps(deps);
    }

    void TACCacheDBBE::ReleaseAll(const NUserService::TPeer& peer) {
        // TODO: Extend to tasks
        Runnning_.RemoveRunningProcess(peer.GetProc());
    }

    bool TACCacheDBBE::IsQuiescent() const noexcept {
        return Inserter_.IsQuiescent(QuiescenceTime_);
    }

    void DefaultErrorHandler(TLog& log, const std::exception& exc) {
        if (/* auto sqlExc = */ dynamic_cast<const NSQLite::TSQLiteError*>(&exc)) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[ACBE]") << "SQLite exception: " << exc.what() << Endl;
        } else if (/* auto yExc = */ dynamic_cast<const yexception*>(&exc)) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[ACBE]") << "yexception: " << exc.what() << Endl;
        } else {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, "EMERG[ACBE]") << "std::exception: " << exc.what() << Endl;
        }
        Y_ABORT_UNLESS(0);
    }
}

bool NACCache::CreateDBIfNeeded(const TString& dbPath, TLog& log, bool recreate) {
    auto checkStatemets = [](NSQLite::TSQLiteDB& db) -> void {
        // Check scheme with SQLStmt preparations.
        NACCachePrivate::TCreateTablesStmt a(db);
        NACCachePrivate::TSetupConnection b(db, true /* enable foreign keys for checks */);
        NACCachePrivate::TACStmts c(db);
        NACCachePrivate::TCASStmts d(db);
        NACCachePrivate::TGcQueriesStmts h(db);
        NACCachePrivate::TStatQueriesStmts i(db);
        NACCachePrivate::TRunningQueriesStmts j(db);
        NACCachePrivate::TValidateTablesStmt v(db);
    };

    (void)NCachesPrivate::CreateDBIfNeeded(dbPath, log, std::move(checkStatemets), "EMERG[ACSERV]", recreate /* in-out */);
    return recreate;
}
