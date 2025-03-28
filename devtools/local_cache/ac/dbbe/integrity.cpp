#include "devtools/local_cache/ac/dbbei.h"
#include "devtools/local_cache/ac/db/db-public.h"
#include "devtools/local_cache/ac/db/cas.h"

#include "devtools/local_cache/common/simple_wqueue.h"
#include "devtools/local_cache/common/dbbe-utils/dbbei_work_queue_wrapper.h"
#include <devtools/local_cache/common/logger-utils/simple_stats.h>

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/yexception.h>
#include <util/generic/list.h>
#include <util/generic/scope.h>
#include <util/string/cast.h>

/// Advanced cleanup wrt to limit
constexpr float CLEANUP_FACTOR = 0.9;

/// TIntegrityHandler implementation.
namespace NACCache {
    using namespace NSQLite;
    using namespace NACCachePrivate;
    constexpr const char EmergencyMsg[] = "EMERG[ACGC]";

    namespace NGCAndFSImpl {
        using namespace NACCache;
        /// Kinds of tasks TGCAndFSHandler performs.
        enum TEnum {
            /// Do GC
            PerformGC,
            /// Release ACS used by running procs
            ReleaseACs,
            MinQueueSize = ReleaseACs + 2
        };

        /// Work item to process in TRunningProcsHandler
        struct TWorkItem {
            TEnum Kind;
        };

        struct TAux {
            // Diagnostics
            const char* Comment;
            // Diagnostics
            TEnum Kind;
            bool Clean;

            TString ToString() const {
                return TString(Comment ? Comment : "");
            }
        };

        bool operator==(const TWorkItem& a, const TWorkItem& b) {
            return a.Kind == b.Kind;
        }

        bool operator!=(const TWorkItem& a, const TWorkItem& b) {
            return !operator==(a, b);
        }
    }
}

template <>
struct THash<NACCache::NGCAndFSImpl::TWorkItem> {
    using TType = NACCache::NGCAndFSImpl::TWorkItem;
    inline size_t operator()(const TType& p) const {
        using namespace NACCache::NGCAndFSImpl;
        return THash<ui64>()(p.Kind);
    }
};

template <>
inline TString ToString<NACCache::NGCAndFSImpl::TWorkItem>(const NACCache::NGCAndFSImpl::TWorkItem& n) {
    return ToString<int>(n.Kind);
}

namespace NACCache {
    class TIntegrityHandler::TImpl : TNonCopyable {
        template <typename T, const char* Emergency>
        friend class NCachesPrivate::THandleExceptions;

        using TWorkItem = NGCAndFSImpl::TWorkItem;
        using TAux = NGCAndFSImpl::TAux;
        using Interface = TIntegrityHandler;

        class TProcessor: public NCachesPrivate::THandleExceptions<TImpl, NGCAndFSImpl::EmergencyMsg> {
            using TBase = NCachesPrivate::THandleExceptions<TImpl, NGCAndFSImpl::EmergencyMsg>;

        public:
            TProcessor(TImpl* parent)
                : NCachesPrivate::THandleExceptions<TImpl, NGCAndFSImpl::EmergencyMsg>(parent)
            {
            }
            std::pair<bool, bool> Process(typename TImpl::TWorkItem& item, typename TImpl::TAux& aux) noexcept {
                auto r = TBase::Process(item, aux);
                TBase::Parent_->MassageQueue();
                return r;
            }
        };

        using TWalkerType = TWalker<NGCAndFSImpl::TWorkItem, TAux, TProcessor>;

    public:
        TImpl(THolder<TSQLiteDB>&& db, bool noBlobIO, ssize_t limit, int maxSize, TStringBuf casStoreDir, const TCriticalErrorHandler& handler, bool casLogging, TLog& log, bool recreate)
            : DB_(std::move(db))
            , Cas_(new TCASManager(*DB_, ToString(casStoreDir), true, noBlobIO ? TFsBlobProcessor::NoIO : TFsBlobProcessor::Regular, casLogging, "blobs", recreate))
            , DBHandler_(*DB_)
            , GCStmts_(*DB_)
            , StatStmts_(*DB_)
            , RunningHandler_(nullptr)
            , ProcessingThread_(maxSize, TProcessor(this))
            , ErrorHandler_(handler)
            , Callback_(&TotalFSSize, &TotalSize, &TotalACs, &TotalBlobs, &LastAccessTime)
            , Log_(log)
            , MasterMode_(0)
            , EnforcedSizeLimit_(-1)
            , SizeLimit_(limit)
            , ReleaseStats_("Release ACs time")
            , RemoveStats_("GC time")
        {
            try {
                AtomicSet(LastAccessCnt, NACCachePrivate::TStatQueriesStmts(*DB_).GetLastAccessNumber());
            } catch (const TSQLiteError& e) {
            }
            AtomicSet(LastAccessTime, MilliSeconds());
        }

        ~TImpl() {
            ReleaseStats_.PrintSummary(Log_);
            RemoveStats_.PrintSummary(Log_);
        }

        void SetRunningProcHandler(TRunningProcsHandler* runningHandler) {
            RunningHandler_ = runningHandler;
        }

        void SetMasterMode(bool isMaster) {
            AtomicSet(MasterMode_, isMaster ? 1 : 0);
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACINT]") << "Set master mode: " << isMaster << Endl;
        }

        bool GetMasterMode() const noexcept {
            return AtomicGet(MasterMode_);
        }

#define SET_REQUEST()                                                  \
    {                                                                  \
        std::unique_lock<std::mutex> lock(Callback_.LockWaitFlag);     \
        AtomicIncrement(Callback_.PendingRequests);                    \
    }                                                                  \
                                                                       \
    Y_DEFER {                                                          \
        intptr_t oldValue = 0;                                         \
        {                                                              \
            std::unique_lock<std::mutex> lock(Callback_.LockWaitFlag); \
            oldValue = AtomicDecrement(Callback_.PendingRequests);     \
        }                                                              \
        if (oldValue == 0) {                                           \
            Callback_.WaitFlag.notify_one();                           \
            AtomicSet(LastAccessTime, MilliSeconds());                 \
        }                                                              \
    };

        TDBBEReturn PutUid(const NACCache::TPutUid& uidInfo) {
            SET_REQUEST();

            auto r = DBHandler_.PutUid(uidInfo, *Cas_, UpdateTimes());
            AtomicAdd(TotalFSSize, r.TotalFSSizeDiff);
            AtomicAdd(TotalSize, r.TotalSizeDiff);
            AtomicAdd(TotalACs, r.ACsDiff);
            AtomicAdd(TotalBlobs, r.BlobDiff);

            if (uidInfo.HasPeer() && uidInfo.GetPeer().HasProc()) {
                RunningHandler_->AddRunningProcess(uidInfo.GetPeer().GetProc(), r.ProcId);
            }
            return TDBBEReturn({r.CopyMode, r.Success});
        }

        TDBBEReturn GetUid(const NACCache::TGetUid& uidInfo) {
            SET_REQUEST();

            auto r = DBHandler_.GetUid(uidInfo, *Cas_, UpdateTimes());
            return TDBBEReturn({r.CopyMode, r.Success});
        }

        TDBBEReturn RemoveUid(const NACCache::TRemoveUid& uidInfo) {
            UpdateTimes();

            SET_REQUEST();

            auto r = DBHandler_.RemoveUid(uidInfo, *Cas_);
            AtomicAdd(TotalFSSize, r.TotalFSSizeDiff);
            AtomicAdd(TotalSize, r.TotalSizeDiff);
            AtomicAdd(TotalACs, r.ACsDiff);
            AtomicAdd(TotalBlobs, r.BlobDiff);

            return TDBBEReturn({r.CopyMode, r.Success});
        }

        TDBBEReturn HasUid(const NACCache::THasUid& uidInfo) {
            SET_REQUEST();

            auto r = DBHandler_.HasUid(uidInfo, UpdateTimes());
            if (r.Success && uidInfo.HasPeer() && uidInfo.GetPeer().HasProc()) {
                RunningHandler_->AddRunningProcess(uidInfo.GetPeer().GetProc(), r.ProcId);
            }
            return TDBBEReturn({r.CopyMode, r.Success});
        }

        void GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
            StatStmts_.GetTaskStats(peer, out);
        }

        void AnalyzeDU(TDiskUsageSummary* out) {
            SET_REQUEST();
            UpdateTimes();

            StatStmts_.AnalyzeDU(out);
        }

        bool PutDeps(const NACCache::TNodeDependencies& deps) {
            UpdateTimes();

            SET_REQUEST();

            return DBHandler_.PutDeps(deps);
        }

        bool ForceGC(const TForceGC& res) {
            AtomicSet(LastAccessTime, MilliSeconds());
            if (!MasterMode_) {
                return false;
            }
            return ForceGC(static_cast<i64>(res.GetTargetSize()));
        }

        void SynchronousGC(const NACCache::TSyncronousGC& config) {
            SET_REQUEST();
            UpdateTimes();

            GCStmts_.SynchronousGC(config, &Callback_, DBHandler_, *Cas_);
        }

        void Initialize() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.ResetLostItems();
            ResetStats();
            WakeUpGC();
            ProcessingThread_.Initialize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Started GC thread" << Endl;
        }

        void Finalize() noexcept {
            {
                std::unique_lock<std::mutex> lock(Callback_.LockWaitFlag);
                AtomicSet(Callback_.ShutdownSignaled, 1);
            }
            Callback_.WaitFlag.notify_all();

            ResetStats();
            ProcessingThread_.Finalize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Stopped GC thread" << Endl;
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Items left in GC: " << Endl;
            ProcessingThread_.Out(Log_);
        }

        void Flush() {
            ProcessingThread_.Flush();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Flushed GC thread" << Endl;
        }

        // Asynchronous access wrt ProcessingThread_.
        void WakeUpGC() {
            UpdateRefCountsOnAcs();
            RestartStatisticsAndGC();
        }

        size_t GetWorkQueueSizeEstimation() const noexcept {
            return ProcessingThread_.Size();
        }

        bool IsQuiescent(int quiescenceTime) const noexcept {
            if (GetWorkQueueSizeEstimation() != 0 || !AtomicGet(Callback_.RCState.CompletedOut) || !AtomicGet(Callback_.GCState.CompletedOut)) {
                return false;
            }

            {
                std::unique_lock<std::mutex> lock(Callback_.LockWaitFlag);
                auto requests = AtomicGet(Callback_.PendingRequests);
                if (requests != 0) {
                    return false;
                }
            }

            ui64 now = MilliSeconds();

            ui64 lastAccess = static_cast<ui64>(AtomicGet(TIntegrityHandler::LastAccessTime));
            if (now <= lastAccess) {
                return false;
            }

            return now >= lastAccess + quiescenceTime;
        }

        TLog& GetLog() noexcept {
            return Log_;
        }

    private:
        intptr_t UpdateTimes() noexcept {
            AtomicSet(LastAccessTime, MilliSeconds());
            return AtomicIncrement(LastAccessCnt);
        }

        bool ForceGC(i64 targetSize) {
            if (targetSize < AtomicGet(SizeLimit_) && targetSize >= 0) {
                AtomicSet(EnforcedSizeLimit_, targetSize);
                ssize_t limit = 0;
                if (!IsSteady(limit)) {
                    WakeUpGC();
                }
            }
            return true;
        }

        /// Relying that ProcessingThread_ does not hold locks during Process call.
        bool ProcessWork(const NGCAndFSImpl::TWorkItem& item, TAux& aux) {
            using namespace NGCAndFSImpl;

            auto kind = item.Kind;
            Y_ABORT_UNLESS(aux.Kind == kind);
            switch (kind) {
                case ReleaseACs:
                    return DoRefCountUpdate();
                case PerformGC:
                    return DoGC();

                case MinQueueSize: // special value
                    Y_UNREACHABLE();
            }
            Y_UNREACHABLE();
        }

        bool Verify(const NGCAndFSImpl::TWorkItem&) const {
            return true;
        }

        void MassageQueue() {
            if (auto inc = ProcessingThread_.ResetLostItems()) {
                WakeUpGC();

                AtomicAdd(CapacityIssues, inc);
            }
        }

        void UpdateRefCountsOnAcs() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.AddItem({ReleaseACs}, {"release acs", ReleaseACs, false}, TWalkerType::NoPositionUpdate);
        }

        // Make sure we have clean statistics
        void RestartStatisticsAndGC() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.AddItem({PerformGC}, {"do gc", PerformGC, true}, TWalkerType::NoPositionUpdate);
        }

        bool IsBelowThreshhold(ssize_t& limit) {
            auto curSize = AtomicGet(TIntegrityHandler::TotalFSSize) + AtomicGet(TIntegrityHandler::TotalDBSize);
            auto lockedSize = AtomicGet(TIntegrityHandler::TotalDBSize);
            auto enforced = AtomicGet(EnforcedSizeLimit_);

            limit = AtomicGet(SizeLimit_);

            if (enforced != -1) {
                limit = enforced;
            }

            auto oldLimit = limit;
            // Try to account for DB size, update limits
            if (lockedSize > limit) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]")
                    << "Cannot reduce size below: " << lockedSize << ", requested: " << limit << Endl;
                limit = Max(limit, lockedSize);
                if (!AtomicCas(enforced != -1 ? &EnforcedSizeLimit_ : &SizeLimit_, limit, oldLimit)) {
                    return false;
                }
                oldLimit = limit;
            }

            if (curSize <= limit) {
                // Reset enforced limit, as it was reached once.
                if (enforced != -1 && !AtomicCas(&EnforcedSizeLimit_, -1, oldLimit)) {
                    return false;
                }
                return true;
            }
            return false;
        }

        void ResetStats() {
            using namespace NGCAndFSImpl;

            // Obtain clean statistics.
            auto out = StatStmts_.GetStatistics();
            auto fsSize = AtomicSwap(&TotalFSSize, out.GetTotalFSSize());
            auto size = AtomicSwap(&TotalSize, out.GetTotalSize());
            auto acs = AtomicSwap(&TotalACs, out.GetUidCount());
            AtomicSet(TotalDBSize, out.GetTotalDBSize());
            auto blobs = AtomicSwap(&TotalBlobs, out.GetBlobCount());
            if (fsSize != (i64)out.GetTotalFSSize()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]")
                    << "Missed FSSize, diff: " << fsSize - (i64)out.GetTotalFSSize() << ", DB ref FSSize: " << (i64)out.GetTotalFSSize() << Endl;
            }
            if (size != (i64)out.GetTotalSize()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]")
                    << "Missed Size, diff: " << size - (i64)out.GetTotalSize() << ", DB ref Size: " << (i64)out.GetTotalSize() << Endl;
            }
            if (acs != out.GetUidCount()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]")
                    << "Missed acs count, diff: " << acs - out.GetUidCount() << ", DB ref count: " << out.GetUidCount() << Endl;
            }
            if (blobs != out.GetBlobCount()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]")
                    << "Missed blobs count, diff: " << blobs - out.GetBlobCount() << ", DB ref count: " << out.GetBlobCount() << Endl;
            }
            AtomicSet(TotalProcs, out.GetProcessesCount());
        }

        bool IsSteady(ssize_t& limit) {
            ResetStats();
            return IsBelowThreshhold(limit);
        }

        // \return true nothing to update
        bool DoRefCountUpdate() {
            AtomicSet(LastAccessTime, MilliSeconds());

            auto start = TInstant::Now();
            Y_DEFER {
                auto end = TInstant::Now();
                ReleaseStats_.UpdateStats(end - start, TStringBuf(""), Log_);
            };

            Callback_.ResetRC();
            GCStmts_.UpdateAcsRefCounts(&Callback_);
            if (AtomicGet(Callback_.RCState.CompletedOut)) {
                // Give GC a chance.
                RestartStatisticsAndGC();
                return true;
            }
            return false;
        }

        // \return true nothing to remove so far
        bool DoGC() {
            AtomicSet(LastAccessTime, MilliSeconds());

            using namespace NGCAndFSImpl;

            if (!AtomicGet(MasterMode_)) {
                AtomicSet(Callback_.GCState.CompletedOut, 1);
                return true;
            }

            ssize_t limit = 0;

            if (IsSteady(limit)) {
                // Wait for WakeUpGC
                AtomicSet(Callback_.GCState.CompletedOut, 1);
                return true;
            }

            auto start = TInstant::Now();
            Y_DEFER {
                auto end = TInstant::Now();
                RemoveStats_.UpdateStats(end - start, TStringBuf(""), Log_);
            };

            // cleanup in advance
            limit = CLEANUP_FACTOR * limit < TIntegrityHandler::TotalDBSize ? limit : CLEANUP_FACTOR * limit;
            // limit accounts for DB size, Callback_ checks for TotalFSSize only,
            Callback_.ResetGC(limit, TIntegrityHandler::TotalDBSize);

            try {
                GCStmts_.CleanSomething(&Callback_, DBHandler_, *Cas_);
            } catch (const TSystemError& err) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR, "ERR[ACGC]")
                    << "ACs cannot be removed, err:" << err.what() << Endl;
            }

            if (AtomicGet(Callback_.GCState.CompletedOut) && IsBelowThreshhold(limit)) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "GC stopped,  limit: " << limit << Endl;
                // Wait for WakeUpGC
                return true;
            }

            RestartStatisticsAndGC();
            return false;
        }

        /// Single SQLite3 connection.
        THolder<TSQLiteDB> DB_;
        THolder<TCASManager> Cas_;
        /// Drivers of updates.
        TACStmts DBHandler_;
        /// Runs in separate single thread. Thus no synchronization for DB_.
        TGcQueriesStmts GCStmts_;
        TStatQueriesStmts StatStmts_;

        /// Notify asynchronously about running processes.
        TRunningProcsHandler* RunningHandler_;

        TWalkerType ProcessingThread_;
        TCriticalErrorHandler ErrorHandler_;

        TCancelCallback Callback_;

        TLog& Log_;

        TAtomic MasterMode_;
        ssize_t EnforcedSizeLimit_;
        ssize_t SizeLimit_;

        NCachesPrivate::TSimpleStats ReleaseStats_;
        NCachesPrivate::TSimpleStats RemoveStats_;
    };

    TIntegrityHandler::TIntegrityHandler(THolder<TSQLiteDB>&& db, bool noBlobIO, ssize_t diskLimit, int maxQueueSize, TStringBuf casStoreDir, const TCriticalErrorHandler& handler, bool casLogging, TLog& log, bool recreate) {
        Ref_.Reset(new TImpl(std::move(db), noBlobIO, diskLimit, maxQueueSize <= 0 ? 100000 : Max(maxQueueSize, static_cast<int>(NGCAndFSImpl::MinQueueSize)), casStoreDir, handler, casLogging, log, recreate));
    }

    TIntegrityHandler::~TIntegrityHandler() {
        auto& log = Ref_->GetLog();
        Ref_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Destroyed GC thread" << Endl;
    }

    void TIntegrityHandler::SetRunningProcHandler(TRunningProcsHandler* runningHandler) {
        Ref_->SetRunningProcHandler(runningHandler);
    }

    void TIntegrityHandler::SetMasterMode(bool isMaster) {
        Ref_->SetMasterMode(isMaster);
    }

    bool TIntegrityHandler::GetMasterMode() const noexcept {
        return Ref_->GetMasterMode();
    }

    TDBBEReturn TIntegrityHandler::PutUid(const NACCache::TPutUid& uidInfo) {
        return Ref_->PutUid(uidInfo);
    }

    TDBBEReturn TIntegrityHandler::GetUid(const NACCache::TGetUid& uidInfo) {
        return Ref_->GetUid(uidInfo);
    }

    TDBBEReturn TIntegrityHandler::RemoveUid(const NACCache::TRemoveUid& uidInfo) {
        return Ref_->RemoveUid(uidInfo);
    }

    TDBBEReturn TIntegrityHandler::HasUid(const NACCache::THasUid& uidInfo) {
        return Ref_->HasUid(uidInfo);
    }

    bool TIntegrityHandler::ForceGC(const TForceGC& setup) {
        return Ref_->ForceGC(setup);
    }

    void TIntegrityHandler::Initialize() {
        Ref_->Initialize();
    }

    void TIntegrityHandler::Finalize() noexcept {
        Ref_->Finalize();
    }

    void TIntegrityHandler::Flush() {
        Ref_->Flush();
    }

    void TIntegrityHandler::WakeUpGC() {
        Ref_->WakeUpGC();
    }

    size_t TIntegrityHandler::GetWorkQueueSizeEstimation() const noexcept {
        return Ref_->GetWorkQueueSizeEstimation();
    }

    bool TIntegrityHandler::IsQuiescent(int quiescenceTime) const noexcept {
        return Ref_->IsQuiescent(quiescenceTime);
    }

    void TIntegrityHandler::GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
        Ref_->GetTaskStats(peer, out);
    }

    void TIntegrityHandler::AnalyzeDU(TDiskUsageSummary* out) {
        Ref_->AnalyzeDU(out);
    }

    bool TIntegrityHandler::PutDeps(const NACCache::TNodeDependencies& deps) {
        return Ref_->PutDeps(deps);
    }

    void TIntegrityHandler::SynchronousGC(const NACCache::TSyncronousGC& config) {
        return Ref_->SynchronousGC(config);
    }

    TAtomic TIntegrityHandler::TotalFSSize(0);
    TAtomic TIntegrityHandler::TotalSize(0);
    TAtomic TIntegrityHandler::TotalDBSize(0);
    TAtomic TIntegrityHandler::TotalBlobs(0);
    TAtomic TIntegrityHandler::TotalACs(0);
    TAtomic TIntegrityHandler::TotalProcs(0);
    TAtomic TIntegrityHandler::LastAccessCnt(0);
    TAtomic TIntegrityHandler::LastAccessTime(0);

    TAtomic TIntegrityHandler::DBLocked(0);
    TAtomic TIntegrityHandler::CapacityIssues(0);
}
