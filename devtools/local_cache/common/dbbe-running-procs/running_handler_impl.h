#pragma once

#include "running_handler.h"

#include "devtools/local_cache/common/simple_wqueue.h"
#include <devtools/local_cache/common/logger-utils/simple_stats.h>
#include "devtools/local_cache/psingleton/systemptr.h"
#include "devtools/local_cache/common/dbbe-utils/dbbei_work_queue_wrapper.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/yexception.h>
#include <util/generic/scope.h>
#include <util/string/cast.h>
#include <util/system/yield.h>

#if defined(_unix_)
#include <sys/resource.h>
#endif

/// TRunningProcsHandler implementation.
namespace NCachesPrivate {
    using namespace NSQLite;

    namespace NRunningProcsImpl {
        /// Kinds of tasks TRunningProcsHandler performs.
        enum TEnum {
            /// Check if process is still alive.
            CheckProcess,
            /// Check DB for running processes.
            AddRunningFromDB,
            /// Clean DB from dead processes.
            MinQueueSize = AddRunningFromDB + 1
        };

        /// Work item to process in TRunningProcsHandler
        struct TWorkItem {
            TWorkItem(TEnum kind = AddRunningFromDB)
                : Kind(kind)
            {
            }
            TWorkItem(TProcessUID pid, TEnum kind)
                : Pid(pid)
                , Kind(kind)
            {
            }
            TProcessUID Pid;
            TEnum Kind;
        };

        inline bool operator==(const TWorkItem& a, const TWorkItem& b) {
            return a.Kind == b.Kind && (a.Kind == CheckProcess ? a.Pid == b.Pid : true);
        }

        inline bool operator!=(const TWorkItem& a, const TWorkItem& b) {
            return !operator==(a, b);
        }

        struct TAux {
            const char* Comment;
            i64 Rowid;
            NRunningProcsImpl::TEnum Kind;

            TString ToString() const {
                return ::ToString(Rowid) + " " + " " + ::ToString<i64>(Kind) + " " + (Comment ? Comment : "");
            }
        };
    }
}

template <>
struct THash<NCachesPrivate::NRunningProcsImpl::TWorkItem> {
    using TType = NCachesPrivate::NRunningProcsImpl::TWorkItem;
    inline size_t operator()(const TType& p) const {
        return THash<ui64>()(p.Kind) + (p.Kind == NCachesPrivate::NRunningProcsImpl::CheckProcess ? THash<TProcessUID>()(p.Pid) : 0);
    }
};

template <>
inline TString ToString<NCachesPrivate::NRunningProcsImpl::TWorkItem>(const NCachesPrivate::NRunningProcsImpl::TWorkItem& n) {
    return ToString(n.Pid) + ":" + ToString<int>(n.Kind);
}

/// TRunningProcsHandler implementation.
namespace NCachesPrivate {
    template <typename GCAndFSHandler, typename DBHandler>
    class TRunningProcsHandler<GCAndFSHandler, DBHandler>::TImpl : TNonCopyable {
        template <typename T, const char* Emergency>
        friend class NCachesPrivate::THandleExceptions;

        using TWorkItem = NRunningProcsImpl::TWorkItem;
        using TAux = NRunningProcsImpl::TAux;
        using Interface = TRunningProcsHandler;
        constexpr static const char EmergencyMsg[] = "EMERG[RP]";

        /// Work item in ProcessingThread_
        class TProcessor: public NCachesPrivate::THandleExceptions<TImpl, EmergencyMsg> {
            using TBase = NCachesPrivate::THandleExceptions<TImpl, EmergencyMsg>;

        public:
            TProcessor(TImpl* parent)
                : TBase(parent)
            {
            }
            std::pair<bool, bool> Process(typename TImpl::TWorkItem& item, typename TImpl::TAux& aux) noexcept {
                auto r = TBase::Process(item, aux);
                TBase::Parent_->MassageQueue();
                if (!r.first) {
                    auto s = TBase::Parent_->ProcessingThread_.Size();
                    // (s - 32) * wait <= 50 msec
                    // Assume there are ~ 32 processes.
                    usleep(s > 32 ? 50 * 1000 / (s - 32) : 50 * 1000);
                }
                return r;
            }
        };

        using TWalkerType = TWalker<NRunningProcsImpl::TWorkItem, TAux, TProcessor>;

    public:
        TImpl(THolder<NSQLite::TSQLiteDB>&& db, int maxSize, GCAndFSHandler& gcHandler, TLog& log, const TCriticalErrorHandler& handler)
            : DB_(std::move(db))
            , RunningStmts_(*DB_)
            , ProcessingThread_(maxSize, TProcessor(this))
            , ErrorHandler_(handler)
            , MaxCtime_(0)
            , GCHandler_(gcHandler)
            , Log_(log)
            , DropProcStats_("DropDeadProc", 10)
        {
        }

        ~TImpl() {
            DropProcStats_.PrintSummary(Log_);
        }

        void Initialize() {
            using namespace NRunningProcsImpl;
            ProcessingThread_.ResetLostItems();
            RestartStream();
            ProcessingThread_.Initialize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Started Proc thread" << Endl;
        }

        void Finalize() noexcept {
            ProcessingThread_.Finalize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Stopped Proc thread" << Endl;
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Items left in Proc: " << Endl;
            ProcessingThread_.Out(Log_);
        }

        void Flush() {
            ProcessingThread_.Flush();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Flushed Proc thread" << Endl;
        }

        // Asynchronous access wrt ProcessingThread_.
        void AddRunningProcess(const NUserService::TProc& proc, i64 rowid) {
            using namespace NRunningProcsImpl;

            TProcessUID p(proc.GetPid(), proc.GetStartTime());
            ProcessingThread_.AddItem({p, CheckProcess}, {"check proc", rowid, CheckProcess});
        }

        size_t GetWorkQueueSizeEstimation() const noexcept {
            return ProcessingThread_.Size();
        }

        // Asynchronous access wrt ProcessingThread_.
        void RemoveRunningProcess(const NUserService::TProc& proc) {
            TWriteGuard lock(CooperativeMutex_);
            CooperativelyRemoved_.insert(TProcessUID(proc.GetPid(), proc.GetStartTime()));
        }

        TLog& GetLog() noexcept {
            return Log_;
        }

    private:
        /// Relying that ProcessingThread_ does not hold locks during Process call.
        bool ProcessWork(NRunningProcsImpl::TWorkItem& item, const TAux& aux) {
            using namespace NRunningProcsImpl;

            bool skipItem = DropCooperativeProcs(item);
            auto rowid = aux.Rowid;
            auto kind = item.Kind;
            switch (kind) {
                case CheckProcess:
                    return skipItem || CheckProcessAndUpdateDB(item, rowid);
                case AddRunningFromDB:
                    Y_ABORT_UNLESS(rowid == -1);
                    return AddFromDB();
                case MinQueueSize: // special value
                    Y_UNREACHABLE();
            }
            Y_UNREACHABLE();
        }

        bool Verify(const NRunningProcsImpl::TWorkItem&) const {
            return true;
        }

        void MassageQueue() {
            using namespace NRunningProcsImpl;
            if (auto inc = ProcessingThread_.ResetLostItems()) {
                AtomicAdd(TRunningProcsHandler::CapacityIssues, inc);

                // Some items were lost since last DB query in ProcessingThread_.
                RestartStream();
            }
        }

        bool DropCooperativeProcs(const NRunningProcsImpl::TWorkItem& item) {
            using namespace NRunningProcsImpl;
            TVector<TProcessUID> pids;
            bool skipItem = false;
            {
                TWriteGuard lock(CooperativeMutex_);
                pids.insert(pids.end(), CooperativelyRemoved_.begin(), CooperativelyRemoved_.end());
                skipItem = item.Kind == CheckProcess && CooperativelyRemoved_.contains(item.Pid);
                CooperativelyRemoved_.clear();
            }

            for (auto& pid : pids) {
                ProcessingThread_.RemoveItem(TWorkItem(pid, CheckProcess));
                RunningStmts_.DropDeadProc(pid);
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Dropped cooperative process from DB: " << ToString(pid) << Endl;
            }
            if (!pids.empty()) {
                GCHandler_.WakeUpGC();
            }
            return skipItem;
        }

        void RestartStream() {
            using namespace NRunningProcsImpl;

            AtomicSet(MaxCtime_, 0);
            ProcessingThread_.AddItem({TProcessUID(), AddRunningFromDB}, {"get procs (from DB)", -1, AddRunningFromDB},
                                      TWalkerType::NoPositionUpdate);
        }

        /// Callback for TProcessor.
        /// Relying on single thread in ProcessingThread_ and/or mutex in db module.
        bool AddFromDB() {
            using namespace NRunningProcsImpl;

            TList<std::pair<TWorkItem, TAux>> list;
            time_t maxTime = AtomicGet(MaxCtime_);
            for (auto& v : RunningStmts_.GetRunningProcs()) {
                // TODO: extract and utilize expected life time of proc as std::get<1>(v)
                list.emplace_back(TWorkItem(std::get<2>(v), CheckProcess),
                                  TAux({"check procs (from DB)", std::get<0>(v), CheckProcess}));
                maxTime = Max(maxTime, std::get<2>(v).GetStartTime());
            }
            if (list.size() == 0) {
                AtomicSet(MaxCtime_, 0);
                return maxTime == 0;
            }
            AtomicSet(MaxCtime_, maxTime);
            ProcessingThread_.AddItems(list, TWalkerType::NoPositionUpdate);
            return false;
        }

        /// Callback for TProcessor.
        /// Relying on single thread in ProcessingThread_ and/or mutex in db module.
        bool CheckProcessAndUpdateDB(NRunningProcsImpl::TWorkItem& item, i64 rowid) {
            if (item.Pid.GetPid() != 0) {
                if (item.Pid.CheckProcess()) {
                    return false /* retry */;
                }
                // Mutate request.
                item.Pid = TProcessUID();
            }

            auto start = TInstant::Now();
            Y_DEFER {
                auto end = TInstant::Now();
                auto pid = ToString(item.Pid);
                DropProcStats_.UpdateStats(end - start, pid, Log_);
            };

            auto stats = RunningStmts_.DropDeadProc(rowid);
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Dropped dead process from DB (" << rowid << "), stats: " << stats << Endl;
            GCHandler_.WakeUpGC();
            return true /* done */;
        }

        /// Runs in separate single thread. Thus no synchronization for DB_.
        THolder<TSQLiteDB> DB_;
        DBHandler RunningStmts_;

        TWalkerType ProcessingThread_;

        TRWMutex CooperativeMutex_;
        THashSet<TProcessUID> CooperativelyRemoved_;

        TCriticalErrorHandler ErrorHandler_;

        time_t MaxCtime_;

        // Notify about dead process.
        GCAndFSHandler& GCHandler_;
        TLog& Log_;

        NCachesPrivate::TSimpleStats DropProcStats_;
    };

    template <typename GCAndFSHandler, typename DBHandler>
    TRunningProcsHandler<GCAndFSHandler, DBHandler>::TRunningProcsHandler(THolder<NSQLite::TSQLiteDB>&& db, GCAndFSHandler& gcHandler, TLog& log, const TCriticalErrorHandler& handler, int maxSize) {
        using namespace NRunningProcsImpl;
        int max = maxSize;
#if defined(_unix_)
        rlimit rlimit;
        Zero(rlimit);
        if (max <= 0 && getrlimit(RLIMIT_NPROC, &rlimit) == 0) {
            max = rlimit.rlim_max + 1;
        }
#endif
        max = max <= 0 ? 100000 : max;
        max = Max(max, static_cast<int>(MinQueueSize));
        Ref_.Reset(new TImpl(std::move(db), max, gcHandler, log, handler));
    }

    template <typename GCAndFSHandler, typename DBHandler>
    TRunningProcsHandler<GCAndFSHandler, DBHandler>::~TRunningProcsHandler() {
        auto& log = Ref_->GetLog();
        Ref_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[RP]") << "Destroyed Proc thread" << Endl;
    }

    template <typename GCAndFSHandler, typename DBHandler>
    void TRunningProcsHandler<GCAndFSHandler, DBHandler>::Initialize() {
        Ref_->Initialize();
    }

    template <typename GCAndFSHandler, typename DBHandler>
    void TRunningProcsHandler<GCAndFSHandler, DBHandler>::Finalize() noexcept {
        Ref_->Finalize();
    }

    template <typename GCAndFSHandler, typename DBHandler>
    void TRunningProcsHandler<GCAndFSHandler, DBHandler>::Flush() {
        Ref_->Flush();
    }

    template <typename GCAndFSHandler, typename DBHandler>
    void TRunningProcsHandler<GCAndFSHandler, DBHandler>::AddRunningProcess(const NUserService::TProc& proc, i64 rowid) {
        Ref_->AddRunningProcess(proc, rowid);
    }

    template <typename GCAndFSHandler, typename DBHandler>
    size_t TRunningProcsHandler<GCAndFSHandler, DBHandler>::GetWorkQueueSizeEstimation() const noexcept {
        return Ref_->GetWorkQueueSizeEstimation();
    }

    template <typename GCAndFSHandler, typename DBHandler>
    void TRunningProcsHandler<GCAndFSHandler, DBHandler>::RemoveRunningProcess(const NUserService::TProc& proc) {
        Ref_->RemoveRunningProcess(proc);
    }

    template <typename GCAndFSHandler, typename DBHandler>
    TAtomic TRunningProcsHandler<GCAndFSHandler, DBHandler>::DBLocked = 0;

    template <typename GCAndFSHandler, typename DBHandler>
    TAtomic TRunningProcsHandler<GCAndFSHandler, DBHandler>::CapacityIssues = 0;
}
