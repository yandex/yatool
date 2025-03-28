#include "devtools/local_cache/toolscache/dbbei.h"
#include "devtools/local_cache/toolscache/db/db-public.h"

#include "devtools/local_cache/common/simple_wqueue.h"
#include "devtools/local_cache/toolscache/fs/fs_ops.h"
#include "devtools/local_cache/common/dbbe-utils/dbbei_work_queue_wrapper.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/threading/future/future.h>

#include <util/generic/yexception.h>
#include <util/generic/list.h>
#include <util/stream/output.h>
#include <util/string/cast.h>

/// TGCAndFSHandler implementation.
namespace NToolsCache {
    using namespace NSQLite;
    using namespace NToolsCachePrivate;
    namespace NGCAndFSImpl {
        using namespace NToolsCache;
        /// Kinds of tasks TGCAndFSHandler performs.
        enum TEnum {
            /// Compute disk usage for resource.
            ComputeDU,
            /// Process stale element
            ProcessStaleResource,
            /// GetChunk/GetChunkAll
            GetChunk,

            /// Do GC
            PerformGC,
            /// DeleteSafely
            DeleteResourceSafely,
            MinQueueSize = DeleteResourceSafely + 1
        };

        /// Work item to process in TRunningProcsHandler
        struct TWorkItem {
            bool HasResource() const {
                return EqualToOneOf(Kind, ComputeDU, DeleteResourceSafely);
            }
            TEnum Kind;
            i64 Rowid;
        };

        struct TAux {
            // ComputeDU
            TSBResource Resource;
            // ComputeDU
            size_t DiskSize;
            // Diagnostics
            i64 Rowid;
            // Diagnostics
            const char* Comment;
            // Diagnostics
            TEnum Kind;

            TString ToString() const {
                return ::ToString(Rowid) + " " + Resource.GetSBId() +
                       " " + ::ToString<i64>(DiskSize) + " " + (Comment ? Comment : "");
            }
        };

        bool operator==(const TWorkItem& a, const TWorkItem& b) {
            // ProcessStaleResource is one per queue, resource is not accounted for.
            return a.Kind == b.Kind && (a.HasResource() ? a.Rowid == b.Rowid : true);
        }

        bool operator!=(const TWorkItem& a, const TWorkItem& b) {
            return !operator==(a, b);
        }
    }
    constexpr const char EmergencyMsg[] = "EMERG[TCGC]";
}

template <>
struct THash<NToolsCache::NGCAndFSImpl::TWorkItem> {
    using TType = NToolsCache::NGCAndFSImpl::TWorkItem;
    inline size_t operator()(const TType& p) const {
        using namespace NToolsCache::NGCAndFSImpl;
        return THash<ui64>()(p.Kind) + (p.HasResource() ? THash<i64>()(p.Rowid) : 0);
    }
};

template <>
inline TString ToString<NToolsCache::NGCAndFSImpl::TWorkItem>(const NToolsCache::NGCAndFSImpl::TWorkItem& n) {
    return ToString<int>(n.Kind) + (n.HasResource() ? ": " + ToString<i64>(n.Rowid) : "");
}

/// TGCAndFSHandler implementation.
namespace NToolsCache {
    class TGCAndFSHandler::TImpl : TNonCopyable {
        template <typename T, const char* Emergency>
        friend class NCachesPrivate::THandleExceptions;

        using TWorkItem = NGCAndFSImpl::TWorkItem;
        using TAux = NGCAndFSImpl::TAux;
        using Interface = TGCAndFSHandler;

        /// Work item in ProcessingThread_
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
        TImpl(THolder<TSQLiteDB>&& db, ssize_t limit, int maxSize, IThreadPool& pool, TLog& log, const TCriticalErrorHandler& handler)
            : DB_(std::move(db))
            , FSStmts_(*DB_)
            , FSModStmts_(*DB_, pool)
            , GCStmts_(*DB_)
            , StatStmts_(*DB_)
            , ProcessingThread_(maxSize, TProcessor(this))
            , ErrorHandler_(handler)
            , Log_(log)
            , MasterMode_(0)
            , EnforcedSizeLimit_(-1)
            , SizeLimit_(limit)
        {
        }

        void Initialize() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.ResetLostItems();
            ResetStats();
            WakeUpGC();
            ProcessingThread_.Initialize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]") << "Started GC thread" << Endl;
        }

        void Finalize() noexcept {
            ProcessingThread_.Finalize();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]") << "Stopped GC thread" << Endl;
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]") << "Items left in GC: " << Endl;
            ProcessingThread_.Out(Log_);
        }

        void Flush() {
            ProcessingThread_.Flush();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]") << "Flushed GC thread" << Endl;
        }

        // Asynchronous access wrt ProcessingThread_.
        void AddResource(const TSBResource& res, i64 rowid) {
            using namespace NGCAndFSImpl;
            if (ProcessingThread_.AddItem({ComputeDU, rowid}, {res, size_t(-1), rowid, "compute du", ComputeDU})) {
                // TODO: coordinate with DB.
                AtomicAdd(TIntegrityHandler::UnknownCount, 1);
            }
        }

        // Asynchronous access wrt ProcessingThread_.
        void WakeUpGC() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.AddItem({GetChunk, 0}, {TSBResource(), size_t(-1), 0, "get chunk", GetChunk}, TWalkerType::NoPositionUpdate);
            RestartStatisticsAndGC();
        }

        void ForceGC(i64 targetSize) {
            if (targetSize < AtomicGet(SizeLimit_) && targetSize >= 0) {
                AtomicSet(EnforcedSizeLimit_, targetSize);
                bool cleanState = false;
                ssize_t limit = 0;
                if (!IsSteady(cleanState, limit)) {
                    WakeUpGC();
                }
            }
        }

        size_t GetWorkQueueSizeEstimation() const noexcept {
            return ProcessingThread_.Size();
        }

        void SetMasterMode(bool isMaster) {
            AtomicSet(MasterMode_, isMaster ? 1 : 0);
        }

        bool GetMasterMode() const noexcept {
            return AtomicGet(MasterMode_);
        }

        TLog& GetLog() noexcept {
            return Log_;
        }

        void GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
            StatStmts_.GetTaskStats(peer, out);
        }

    private:
        /// Relying that ProcessingThread_ does not hold locks during Process call.
        bool ProcessWork(const NGCAndFSImpl::TWorkItem& item, TAux& aux) {
            using namespace NGCAndFSImpl;

            auto kind = item.Kind;
            Y_ABORT_UNLESS(aux.Rowid == item.Rowid && aux.Kind == kind);
            switch (kind) {
                case ComputeDU:
                    // May mutate request;
                    ComputeSizeSafelyAndUpdateDB(aux);
                    return true /* done */;
                case ProcessStaleResource:
                    ProcessUnusedUnknown(aux);
                    return true /* done */;
                // Compute disk usage fixup stream
                case GetChunk:
                    return GetChunkToCompute();
                // GC stream
                case PerformGC:
                    Y_ABORT_UNLESS(aux.Rowid == 0 && item.Rowid == 0 && aux.DiskSize == size_t(-1));
                    // Continue fetching if there was something to remove.
                    return GetItemToRemove();
                case DeleteResourceSafely:
                    RemoveSafely(aux, RemoveUnconditionally);
                    return true /* done */;

                case MinQueueSize: // special value
                    Y_UNREACHABLE();
            }
            Y_UNREACHABLE();
        }

        bool Verify(const NGCAndFSImpl::TWorkItem& item) const {
            using namespace NGCAndFSImpl;
            return !item.HasResource() || item.Rowid > 0;
        }

        void MassageQueue() {
            using namespace NGCAndFSImpl;
            if (auto inc = ProcessingThread_.ResetLostItems()) {
                WakeUpGC();

                AtomicAdd(TGCAndFSHandler::CapacityIssues, inc);
            }
        }

        // Make sure we have clean statistics
        void RestartStatisticsAndGC() {
            using namespace NGCAndFSImpl;
            ProcessingThread_.AddItem({PerformGC, 0}, {TSBResource(), size_t(-1), 0, "do gc", PerformGC}, TWalkerType::NoPositionUpdate);
        }

        // \return true if end-of-stream reached
        bool GetChunkToCompute() {
            using namespace NGCAndFSImpl;

            // Need to guarantee progress with DU computations.
            // And avoid the same element returned here multiple times.
            if (!ProcessingThread_.CheckNoPromote({ProcessStaleResource, 0})) {
                if (auto s = GCStmts_.GetStaleToClean(); !s.Empty()) {
                    ProcessingThread_.RemoveItem({ComputeDU, s.GetRef()});
                    ProcessingThread_.AddItem({ProcessStaleResource, s.GetRef()}, {TSBResource(), size_t(-1), s.GetRef(), "unused unknown", ProcessStaleResource});
                    return false;
                }
            }

            auto vec = FSStmts_.GetDirsToComputeSize(false /* those safe to remove are queried first*/);
            if (vec.size() == 0) {
                vec = FSStmts_.GetDirsToComputeSize(true /* all resources are queried*/);
            }
            if (vec.size() == 0) {
                return true;
            }
            TList<std::pair<TWorkItem, TAux>> list;
            for (auto& v : vec) {
                list.emplace_back(TWorkItem({ComputeDU, v.first}), TAux({v.second, size_t(-1), v.first, "compute du (from DB)", ComputeDU}));
            }
            ProcessingThread_.AddItems(list, TWalkerType::NoPositionUpdate);
            return false;
        }

        bool IsBelowThreshhold(bool& cleanState, ssize_t& limit) {
            using namespace NGCAndFSImpl;

            auto curSize = AtomicGet(TIntegrityHandler::TotalSize) + AtomicGet(TIntegrityHandler::TotalDBSize);
            auto lockedSize = AtomicGet(TIntegrityHandler::TotalDBSize) + AtomicGet(TIntegrityHandler::TotalSizeLocked);
            auto enforced = AtomicGet(EnforcedSizeLimit_);

            cleanState = AtomicGet(TIntegrityHandler::UnknownCount) == 0;
            limit = AtomicGet(SizeLimit_);

            if (enforced != -1) {
                limit = enforced;
            }

            auto oldLimit = limit;
            if (lockedSize > limit && cleanState) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]")
                    << "Cannot reduce size below: " << lockedSize << ", requested: " << limit << Endl;
                limit = Max(limit, lockedSize);
                if (!AtomicCas(enforced != -1 ? &EnforcedSizeLimit_ : &SizeLimit_, limit, oldLimit)) {
                    return false;
                }
                oldLimit = limit;
            }

            if (curSize <= limit) {
                // Reset enforced limit
                if (cleanState) {
                    if (enforced != -1 && !AtomicCas(&EnforcedSizeLimit_, -1, oldLimit)) {
                        return false;
                    }
                    ProcessingThread_.RemoveItem({GetChunk, 0});
                }
                // Wait for TIntegrityHandler::UnknownCount to become 0 from GetChunk stream
                return true;
            }
            return false;
        }

        void ResetStats() {
            using namespace NGCAndFSImpl;

            // Obtain clean statistics.
            auto out = StatStmts_.GetStatistics();
            AtomicSet(TIntegrityHandler::UnknownCount, out.GetNonComputedCount());
            AtomicSet(TIntegrityHandler::TotalSize, out.GetTotalKnownSize());
            AtomicSet(TIntegrityHandler::TotalSizeLocked, out.GetTotalKnownSizeLocked());
            AtomicSet(TIntegrityHandler::TotalDBSize, out.GetTotalDBSize());
            AtomicSet(TIntegrityHandler::TotalTools, out.GetToolCount());
            AtomicSet(TIntegrityHandler::TotalProcs, out.GetProcessesCount());
        }

        bool IsSteady(bool& cleanState, ssize_t& limit) {
            ResetStats();
            return IsBelowThreshhold(cleanState, limit);
        }

        // \return true if end-of-stream reached
        bool GetItemToRemove() {
            using namespace NGCAndFSImpl;

            bool cleanState = false;
            ssize_t limit = 0;

            if (IsSteady(cleanState, limit)) {
                return true;
            }

            auto r = GCStmts_.GetSomethingToClean();
            if (r.Empty()) {
                if (cleanState) {
                    LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]")
                        << "Cannot reduce size below limit: " << limit << ", some process consumes disk space" << Endl;
                }
                // Wait for WakeUpGC
                return true;
            }
            i64 id = 0;
            size_t diskSize = 0;
            std::tie(id, diskSize) = r.GetRef();
            ProcessingThread_.AddItem({DeleteResourceSafely, id}, {TSBResource(), diskSize, id, "capacity removal", DeleteResourceSafely});
            return false;
        }

        /// Mutates aux
        void ComputeSizeSafelyAndUpdateDB(TAux& aux) {
            if (aux.DiskSize == size_t(-1)) {
                size_t diskSize(-1);
                try {
                    diskSize = ComputeSize(aux.Resource);
                } catch (const TSystemError& err) {
                    LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCGC]") << "Resource " << aux.Resource << "(" << aux.Rowid << ") is broken, cannot compute size, err:" << err.what() << Endl;
                    diskSize = 1;
                }

                if (diskSize == size_t(-1)) {
                    return;
                }

                aux.DiskSize = diskSize;
            }
            FSStmts_.SetComputedSize(aux.Rowid, aux.DiskSize);
            AtomicAdd(TIntegrityHandler::UnknownCount, -1);
            AtomicAdd(TIntegrityHandler::TotalSize, aux.DiskSize);
            RestartStatisticsAndGC();
        }

        void ProcessUnusedUnknown(const TAux& aux) {
            using namespace NGCAndFSImpl;
            RemoveSafely({TSBResource(), size_t(-1), aux.Rowid, "stale resource", DeleteResourceSafely}, KeepIfUseful);
        }

        void RemoveSafely(const TAux& aux, EKeepDir keep) {
            if (!AtomicGet(MasterMode_) && keep == RemoveUnconditionally) {
                return;
            }

            if (keep == RemoveUnconditionally) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]")
                    << "Going to remove toolid=" << aux.Rowid
                    << ". Comment: " << (aux.Comment ? aux.Comment : "") << Endl;
            } else {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]")
                    << "Going to check toolid=" << aux.Rowid
                    << " validity. Comment: " << (aux.Comment ? aux.Comment : "") << Endl;
            }

            using namespace NGCAndFSImpl;

            ProcessingThread_.RemoveItem({ComputeDU, aux.Rowid});
            ProcessingThread_.RemoveItem({ProcessStaleResource, 0});

            bool cleanState = false;
            ssize_t limit = 0;
            if (keep == RemoveUnconditionally && IsBelowThreshhold(cleanState, limit)) {
                return;
            }

            try {
                auto f = FSModStmts_.SafeDeleteDir(aux.Rowid, keep);
                auto sb = f.first.GetValueSync();
                if (!f.second) {
                    // Sb is installed and was not removed.
                    Y_ABORT_UNLESS(keep == KeepIfUseful);
                    TAux tempAux({sb, size_t(-1), aux.Rowid, "compute du (from DB)", ComputeDU});
                    ComputeSizeSafelyAndUpdateDB(tempAux);
                    return;
                }

                if (!CheckResourceDirDeleted(sb)) {
                    LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "ERR[TCGC]")
                        << "Resource " << sb << "(" << aux.Rowid << ") was NOT removed (" << f.second << ")" << Endl;
                    return;
                }
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]")
                    << "Resource " << sb << "(" << aux.Rowid << ") was removed (" << f.second << ")" << Endl;
            } catch (const TSystemError& err) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR, "ERR[TCGC]")
                    << "Resource with id " << aux.Rowid << " cannot be removed, err:" << err.what() << Endl;
            }

            if (aux.DiskSize != size_t(-1)) {
                AtomicAdd(TIntegrityHandler::TotalSize, -aux.DiskSize);
            } else {
                AtomicAdd(TIntegrityHandler::UnknownCount, -1);
            }
            RestartStatisticsAndGC();
        }

    private:
        /// Runs in separate single thread. Thus no synchronization for DB_.
        THolder<TSQLiteDB> DB_;
        TFsFillerStmts FSStmts_;
        TFsModStmts FSModStmts_;
        TGcQueriesStmts GCStmts_;
        TStatQueriesStmts StatStmts_;

        TWalkerType ProcessingThread_;

        TCriticalErrorHandler ErrorHandler_;

        TLog& Log_;
        TAtomic MasterMode_;
        ssize_t EnforcedSizeLimit_;
        ssize_t SizeLimit_;
    };

    TGCAndFSHandler::TGCAndFSHandler(THolder<TSQLiteDB>&& db, ssize_t limit, IThreadPool& pool, TLog& log, const TCriticalErrorHandler& handler, int maxSize) {
        using namespace NGCAndFSImpl;
        Ref_.Reset(new TImpl(std::move(db), limit, maxSize <= 0 ? 100000 : Max(maxSize, static_cast<int>(MinQueueSize)), pool, log, handler));
    }

    TGCAndFSHandler::~TGCAndFSHandler() {
        auto& log = Ref_->GetLog();
        Ref_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCGC]") << "Destroyed GC thread" << Endl;
    }

    void TGCAndFSHandler::Initialize() {
        Ref_->Initialize();
    }

    void TGCAndFSHandler::Finalize() noexcept {
        Ref_->Finalize();
    }

    void TGCAndFSHandler::Flush() {
        Ref_->Flush();
    }

    void TGCAndFSHandler::AddResource(const TSBResource& res, i64 rowid) {
        Ref_->AddResource(res, rowid);
    }

    void TGCAndFSHandler::WakeUpGC() {
        Ref_->WakeUpGC();
    }

    void TGCAndFSHandler::ForceGC(i64 targetSize) {
        Ref_->ForceGC(targetSize);
    }

    size_t TGCAndFSHandler::GetWorkQueueSizeEstimation() const noexcept {
        return Ref_->GetWorkQueueSizeEstimation();
    }

    void TGCAndFSHandler::SetMasterMode(bool isMaster) {
        Ref_->SetMasterMode(isMaster);
    }

    bool TGCAndFSHandler::GetMasterMode() const noexcept {
        return Ref_->GetMasterMode();
    }

    void TGCAndFSHandler::GetTaskStats(const NUserService::TPeer& peer, TTaskStatus* out) {
        Ref_->GetTaskStats(peer, out);
    }

    TAtomic TGCAndFSHandler::DBLocked(0);
    TAtomic TGCAndFSHandler::CapacityIssues(0);
}
