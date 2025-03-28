#include "devtools/local_cache/toolscache/dbbei.h"
#include "devtools/local_cache/toolscache/db/db-public.h"

#include "devtools/local_cache/toolscache/fs/fs_ops.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/threading/future/future.h>

#include <util/generic/yexception.h>
#include <util/generic/list.h>
#include <util/string/cast.h>

/// TIntegrityHandler implementation.
namespace NToolsCache {
    using namespace NSQLite;
    using namespace NToolsCachePrivate;

    class TIntegrityHandler::TImpl : TNonCopyable {
    public:
        TImpl(THolder<TSQLiteDB>&& db, TGCAndFSHandler& fsHandler, TRunningProcsHandler& runningHandler, IThreadPool& pool, TLog& log)
            : DB_(std::move(db))
            , Inserter_(*DB_)
            , Locker_(*DB_)
            , FSHandler_(fsHandler)
            , RunningHandler_(runningHandler)
            , ThreadPool_(pool)
            , Log_(log)
            , Master_(false)
        {
            try {
                AtomicSet(LastAccessCnt, NToolsCachePrivate::TStatQueriesStmts(*DB_).GetLastAccessNumber());
            } catch (const TSQLiteError& e) {
            }
            AtomicSet(LastAccessTime, MilliSeconds());
        }

        void SetMasterMode(bool isMaster) {
            Master_ = isMaster;
            if (!isMaster) {
                DataToCleanup_.clear();
            }
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "Set master mode: " << isMaster << Endl;
        }

        bool GetMasterMode() const noexcept {
            return Master_;
        }

        void InsertResource(const TResourceUsed& res) {
            InsertToDB(
                [this, &res](TAtomic lastAccess, bool refill, TNotifyStmts::TVecOut& gc) -> std::tuple<i64, i64, size_t> {
                    return Inserter_.UpdateResourceUsage(lastAccess, res, refill, gc);
                },
                res, res.GetResource());
        }

        void InsertService(const TServiceStarted& serv, TServiceInfo* out) {
            InsertToDB(
                [this, &serv, out](TAtomic lastAccess, bool refill, TNotifyStmts::TVecOut& gc) -> std::tuple<i64, i64, size_t> {
                    return Inserter_.UpdateServiceUsage(lastAccess, serv, refill, gc, out);
                },
                serv, serv.GetService().GetResource());
        }

        bool ForceGC(const TForceGC& res) {
            AtomicSet(LastAccessTime, MilliSeconds());
            return ForceGC(static_cast<i64>(res.GetTargetSize()));
        }

        // TODO: process exceptions
        bool ForceGC(TMaybe<i64> targetSize = TMaybe<i64>()) {
            AtomicSet(LastAccessTime, MilliSeconds());
            if (!Master_) {
                return false;
            }

            TList<NThreading::TFuture<TSBResource>> futures;
            for (auto& res : DataToCleanup_) {
                futures.emplace_back(RemoveAtomically(res, false, ThreadPool_));
            }
            auto sz = DataToCleanup_.size();
            DataToCleanup_.clear();
            for (auto& f : futures) {
                auto sb = f.GetValueSync();
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "Resource " << sb << " was removed by explicit request" << Endl;
            }
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "ForceGC done, removed " << sz << " resources" << Endl;

            if (targetSize) {
                FSHandler_.ForceGC(targetSize.GetRef());
            }
            return sz > 0;
        }

        void LockResource(const TSBResource& resource) {
            UpdateTimes();
            {
                auto it = std::find(DataToCleanup_.begin(), DataToCleanup_.end(), resource);
                if (it != DataToCleanup_.end()) {
                    DataToCleanup_.erase(it);
                }
            }

            while (true) {
                try {
                    Locker_.LockResource(resource);
                    break;
                } catch (const TSQLiteError& e) {
                    if (e.GetErrorCode() == SQLITE_FULL && ForceGC()) {
                        // retry
                        continue;
                    }
                    throw;
                }
            };
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "Resource " << resource << " is locked" << Endl;
        }

        void UnlockSBResource(const TSBResource& resource) {
            UpdateTimes();
            while (true) {
                try {
                    Locker_.UnlockSBResource(resource);
                    break;
                } catch (const TSQLiteError& e) {
                    if (e.GetErrorCode() == SQLITE_FULL && ForceGC()) {
                        // retry
                        continue;
                    }
                    throw;
                }
            };
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "Resource " << resource << " is unlocked" << Endl;
        }

        void UnlockAllResources(const NUserService::TPeer& peer) {
            AtomicSet(LastAccessTime, MilliSeconds());
            while (true) {
                try {
                    Locker_.UnlockAllResources();
                    break;
                } catch (const TSQLiteError& e) {
                    if (e.GetErrorCode() == SQLITE_FULL && ForceGC()) {
                        // retry
                        continue;
                    }
                    throw;
                }
            };
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]") << "All resources are unlocked, request from " << peer << Endl;
        }

    private:
        intptr_t UpdateTimes() noexcept {
            AtomicSet(LastAccessTime, MilliSeconds());
            return AtomicIncrement(LastAccessCnt);
        }

        template <typename InsertFunc, typename Request>
        void InsertToDB(InsertFunc&& func, const Request& req, const TSBResource& resource) {
            {
                auto it = std::find(DataToCleanup_.begin(), DataToCleanup_.end(), resource);
                if (it != DataToCleanup_.end()) {
                    DataToCleanup_.erase(it);
                }
            }

            i64 toolId = -1, runningId = -1;
            size_t diskSpace = -1;
            {
                TNotifyStmts::TVecOut temp;
                auto accessCnt = UpdateTimes();
                bool refill = DataToCleanup_.size() > 5;
                while (true) {
                    temp.clear();
                    try {
                        std::tie(toolId, runningId, diskSpace) = func(accessCnt, refill, temp);
                        break;
                    } catch (const TSQLiteError& e) {
                        if (e.GetErrorCode() == SQLITE_FULL && ForceGC()) {
                            // retry
                            continue;
                        }
                        throw;
                    }
                };
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCINT]")
                    << "Insert request " << req << "(toolid=" << toolId << ", procid=" << runningId << ")" << Endl;
                if (refill && Master_) {
                    DataToCleanup_ = temp;
                }
            }

            RunningHandler_.AddRunningProcess(req.GetPeer().GetProc(), runningId);

            if (diskSpace == size_t(-1)) {
                FSHandler_.AddResource(resource, toolId);
            }
        }

        /// Single SQLite3 connection.
        THolder<TSQLiteDB> DB_;
        /// Drivers of updates.
        TNotifyStmts Inserter_;
        TLockResourceStmts Locker_;

        /// FS and GC handling to notify asynchronously.
        TGCAndFSHandler& FSHandler_;
        /// Notify asynchronously about running processes.
        TRunningProcsHandler& RunningHandler_;

        /// Relies on single instance of TIntegrityHandler.
        TNotifyStmts::TVecOut DataToCleanup_;

        /// Thread pool for FS operations.
        IThreadPool& ThreadPool_;
        /// Thread pool for FS operations.
        TLog& Log_;

        /// Whether destructive operations are permitted.
        bool Master_;
    };

    TIntegrityHandler::TIntegrityHandler(THolder<TSQLiteDB>&& db, TGCAndFSHandler& fsHandler, TRunningProcsHandler& runningHandler, IThreadPool& pool, TLog& log) {
        Ref_.Reset(new TImpl(std::move(db), fsHandler, runningHandler, pool, log));
    }

    TIntegrityHandler::~TIntegrityHandler() {
    }

    void TIntegrityHandler::SetMasterMode(bool isMaster) {
        Ref_->SetMasterMode(isMaster);
    }

    bool TIntegrityHandler::GetMasterMode() const noexcept {
        return Ref_->GetMasterMode();
    }

    void TIntegrityHandler::InsertResource(const TResourceUsed& res) {
        Ref_->InsertResource(res);
    }

    void TIntegrityHandler::InsertService(const TServiceStarted& serv, TServiceInfo* out) {
        Ref_->InsertService(serv, out);
    }

    bool TIntegrityHandler::ForceGC(const TForceGC& setup) {
        return Ref_->ForceGC(setup);
    }

    void TIntegrityHandler::LockResource(const TSBResource& res) {
        Ref_->LockResource(res);
    }

    void TIntegrityHandler::UnlockSBResource(const TSBResource& res) {
        Ref_->UnlockSBResource(res);
    }

    void TIntegrityHandler::UnlockAllResources(const NUserService::TPeer& peer) {
        Ref_->UnlockAllResources(peer);
    }

    TAtomic TIntegrityHandler::TotalSize(0);
    TAtomic TIntegrityHandler::TotalSizeLocked(0);
    TAtomic TIntegrityHandler::TotalDBSize(0);
    TAtomic TIntegrityHandler::TotalTools(0);
    TAtomic TIntegrityHandler::TotalProcs(0);
    TAtomic TIntegrityHandler::UnknownCount(0);
    TAtomic TIntegrityHandler::LastAccessCnt(0);
    TAtomic TIntegrityHandler::LastAccessTime(0);
}
