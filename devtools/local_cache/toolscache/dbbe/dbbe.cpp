#include "devtools/local_cache/toolscache/dbbei.h"
#include "devtools/local_cache/toolscache/db/db-public.h"

#include <devtools/local_cache/common/dbbe-utils/create_db.h>

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/ptr.h>
#include <util/string/cast.h>
#include <util/system/datetime.h>
#include <util/system/fs.h>

namespace NToolsCache {
    // Need to keep at least one open connection for in-memory DB.
    class TToolsCacheDBBE::TDB {
    public:
        TDB(TStringBuf dbPath, bool createTables)
            : DB_(new NSQLite::TSQLiteDB(ToString(dbPath), SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX))
        {
            if (createTables) {
                NToolsCachePrivate::TCreateTablesStmt a(*DB_);
            }
            NToolsCachePrivate::TSetupConnection b(*DB_);
        }

        NSQLite::TSQLiteDB* Release() {
            return DB_.Release();
        }

    private:
        THolder<NSQLite::TSQLiteDB> DB_;
    };

    TToolsCacheDBBE::TToolsCacheDBBE(TStringBuf dbPath, ssize_t limit, IThreadPool& pool, TLog& log, const TCriticalErrorHandler& handler, int procMaxSize, int gcMaxSize, int quiescenceTime)
        : GCHandlerDB_(new TDB(dbPath, true))
        , RunningHandlerDB_(new TDB(dbPath, false))
        , InserterHandlerDB_(new TDB(dbPath, false))
        , GC_(THolder<NSQLite::TSQLiteDB>(GCHandlerDB_->Release()), limit, pool, log, handler, gcMaxSize)
        , Runnning_(THolder<NSQLite::TSQLiteDB>(RunningHandlerDB_->Release()), GC_, log, handler, procMaxSize)
        , Inserter_(THolder<NSQLite::TSQLiteDB>(InserterHandlerDB_->Release()), GC_, Runnning_, pool, log)
        , Log_(log)
        , PollerConf_(PollAll)
        , QuiescenceTime_(quiescenceTime)
        , Initialized_(false)
    {
    }

    TToolsCacheDBBE::~TToolsCacheDBBE() {
        Finalize();
    }

    // @{
    void TToolsCacheDBBE::InsertResource(const TResourceUsed& resource) {
        Inserter_.InsertResource(resource);
    }

    void TToolsCacheDBBE::InsertService(const TServiceStarted& serv, TServiceInfo* out) {
        Inserter_.InsertService(serv, out);
    }

    bool TToolsCacheDBBE::ForceGC(const TForceGC& setup) {
        return Inserter_.ForceGC(setup);
    }

    void TToolsCacheDBBE::LockResource(const TSBResource& resource) {
        Inserter_.LockResource(resource);
    }

    void TToolsCacheDBBE::UnlockSBResource(const TSBResource& resource) {
        Inserter_.UnlockSBResource(resource);
    }

    void TToolsCacheDBBE::UnlockAllResources(const NUserService::TPeer& peer) {
        Inserter_.UnlockAllResources(peer);
    }
    // @}

    void TToolsCacheDBBE::SetMasterMode(bool isMaster) {
        GC_.SetMasterMode(isMaster);
        Inserter_.SetMasterMode(isMaster);
    }

    bool TToolsCacheDBBE::GetMasterMode() const noexcept {
        return GC_.GetMasterMode() || Inserter_.GetMasterMode();
    }

    void TToolsCacheDBBE::Initialize(EPollersConf conf) {
        PollerConf_ = conf;
        if (PollerConf_ & PollGCAndFS) {
            AtomicSet(TGCAndFSHandler::CapacityIssues, 0);
            AtomicSet(TGCAndFSHandler::DBLocked, 0);
            GC_.Initialize();
        }
        if (PollerConf_ & PollProcs) {
            AtomicSet(TRunningProcsHandler::CapacityIssues, 0);
            AtomicSet(TRunningProcsHandler::DBLocked, 0);
            Runnning_.Initialize();
        }
        Initialized_ = true;
    }

    void TToolsCacheDBBE::Finalize() noexcept {
        if (!Initialized_) {
            return;
        }
        Runnning_.Finalize();
        GC_.Finalize();
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Restarted transaction procs=" << TRunningProcsHandler::DBLocked << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Restarted transaction gc=" << TGCAndFSHandler::DBLocked << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Queue capacity issues procs=" << TRunningProcsHandler::CapacityIssues << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Queue capacity issues gc=" << TGCAndFSHandler::CapacityIssues << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Queue size procs=" << Runnning_.GetWorkQueueSizeEstimation() << Endl;
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCBE]") << "Queue size gc=" << GC_.GetWorkQueueSizeEstimation() << Endl;
        PollerConf_ = PollAll;
        Initialized_ = false;
    }

    void TToolsCacheDBBE::Flush() {
        Runnning_.Flush();
        GC_.Flush();
    }

    size_t TToolsCacheDBBE::GetWorkQueueSizeEstimation() const noexcept {
        return Runnning_.GetWorkQueueSizeEstimation() + GC_.GetWorkQueueSizeEstimation();
    }

    void TToolsCacheDBBE::GetStats(TStatus* out) const noexcept {
        Y_ABORT_UNLESS(GC_.GetMasterMode() == Inserter_.GetMasterMode());

        out->SetTotalKnownSize(AtomicGet(TIntegrityHandler::TotalSize));
        out->SetTotalKnownSizeLocked(AtomicGet(TIntegrityHandler::TotalSizeLocked));
        out->SetNonComputedCount(AtomicGet(TIntegrityHandler::UnknownCount));
        out->SetMaster(Inserter_.GetMasterMode());
        out->SetTotalDBSize(AtomicGet(TIntegrityHandler::TotalDBSize));
        out->SetToolCount(AtomicGet(TIntegrityHandler::TotalTools));
        out->SetProcessesCount(AtomicGet(TIntegrityHandler::TotalProcs));
    }

    void TToolsCacheDBBE::GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
        GC_.GetTaskStats(peer, out);
    }

    bool TToolsCacheDBBE::IsQuiescent() const noexcept {
        if ((PollerConf_ & PollGCAndFS) && GC_.GetWorkQueueSizeEstimation() != 0) {
            return false;
        }

        ui64 now = MilliSeconds();

        ui64 lastAccess = static_cast<ui64>(AtomicGet(TIntegrityHandler::LastAccessTime));
        if (now <= lastAccess) {
            return false;
        }

        return now >= lastAccess + QuiescenceTime_;
    }

    void DefaultErrorHandler(TLog& log, const std::exception& exc) {
        if (/* auto sqlExc = */ dynamic_cast<const NSQLite::TSQLiteError*>(&exc)) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCBE]") << "SQLite exception: " << exc.what() << Endl;
        } else if (/* auto yExc = */ dynamic_cast<const yexception*>(&exc)) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCBE]") << "yexception: " << exc.what() << Endl;
        } else {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, "EMERG[TCBE]") << "std::exception: " << exc.what() << Endl;
        }
        Y_ABORT_UNLESS(0);
    }
}

int NToolsCache::CreateDBIfNeeded(const TString& dbPath, TLog& log) {
    auto checkStatemets = [](NSQLite::TSQLiteDB& db) -> void {
        // Check scheme with SQLStmt preparations.
        NToolsCachePrivate::TCreateTablesStmt a(db);
        NToolsCachePrivate::TValidateTablesStmt v(db);
        NToolsCachePrivate::TSetupConnection b(db);
        NToolsCachePrivate::TNotifyStmts d(db);
        NToolsCachePrivate::TLockResourceStmts e(db);
        NToolsCachePrivate::TFsFillerStmts f(db);
        TFakeThreadPool pool;
        NToolsCachePrivate::TFsModStmts g(db, pool);
        NToolsCachePrivate::TGcQueriesStmts h(db);
        NToolsCachePrivate::TStatQueriesStmts i(db);
        NToolsCachePrivate::TRunningQueriesStmts j(db);
        NToolsCachePrivate::TServiceVersionQuery k(db);
    };

    bool recreate = false;
    auto holder = NCachesPrivate::CreateDBIfNeeded(dbPath, log, std::move(checkStatemets), "EMERG[TCSERV]", recreate /* in-out */);

    NSQLite::TSQLiteDB rodb(dbPath, SQLITE_OPEN_URI | SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
    return NToolsCachePrivate::TServiceVersionQuery(rodb).GetServiceVersion("ya-tc");
}
