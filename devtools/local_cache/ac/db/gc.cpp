#include "db-private.h"

#include <library/cpp/logger/global/rty_formater.h>

#include <util/folder/path.h>
#include <util/generic/yexception.h>
#include <util/string/cast.h>
#include <util/system/datetime.h>

namespace {
    static const char* GC_QUERIES_NAMES[] = {
        "@AcsRef",
        "@AgeLimit",
        "@BlobRef",
        "@PrevBlobId",
        "@PrevLastAccess",
        "@PrevLastAccessTime",
        "@RequestCount",
        "@ReqRowid",
        "@TaskRef"};
}

/// TGcQueriesStmts implementation.
namespace NACCachePrivate {
    using namespace NACCache;

    struct TGcQueriesStmts::TImpl::TAsyncRemovalIterator {
        TAsyncRemovalIterator(TImpl& parent, NACCache::TCancelCallback& callback)
            : Parent(parent)
            , Callback(callback)
        {
        }

        void InitInner() {
            LastAccess = AtomicGet(Callback.GCState.LastAccessInOut);
            Parent.Get(GCNextAny).Reset();
            Parent.Get(GCNextAny).Bind("@PrevLastAccess", LastAccess);
            LastStepResult = false;
            RemovedCount = 0;
        }

        // Next method for inner loop
        bool NextInner() {
            for (; RemovedCount < MaxRemoveCount && (LastStepResult = Parent.Get(GCNextAny).Step());) {
                AcRowid = Parent.GetColumnInt64(GCNextAny, 0, "AcsRef");
                LastAccess = Parent.GetColumnInt64(GCNextAny, 1, "LastAccess");
                i64 reqCount = Parent.GetColumnInt64(GCNextAny, 2, "RequestCount");
                if (reqCount > 0) {
                    continue;
                }
                ++RemovedCount;
                return true;
            }
            return false;
        }

        i64 Get() const noexcept {
            return AcRowid;
        }

        bool StopInnerCondition() const noexcept {
            // Ignore minor races here, do not acquire Callback.LockWaitFlag
            return Callback.IsLimitReached() || Callback.IsCancellationPending();
        }

        // Next method for outer loop
        bool NextOuter() {
            if (Callback.IsCancellationPending()) {
                return false;
            }
            return NextOuterGraceful();
        }

        // Next method for outer loop
        bool NextOuterGraceful() {
            if (AtomicGet(Callback.GCState.CompletedOut)) {
                return false;
            }

            if (Callback.IsLimitReached()) {
                AtomicSet(Callback.GCState.CompletedOut, 1);
                return false;
            }

            if (!LastStepResult && AtomicGet(Callback.GCState.LastAccessInOut) == 0) {
                // Loop started with first entry and exited when there is no data
                AtomicSet(Callback.GCState.CompletedOut, 1);
                return false;
            }

            if (LastStepResult) {
                AtomicSet(Callback.GCState.LastAccessInOut, LastAccess);
            } else {
                // Start from the beginning
                AtomicSet(Callback.GCState.LastAccessInOut, 0);
            }
            return true;
        }

        TImpl& Parent;
        NACCache::TCancelCallback& Callback;

        TAtomic LastAccess = 0;
        int RemovedCount = 0;
        bool LastStepResult = false;
        i64 AcRowid = -1;

        constexpr static int MaxRemoveCount = 500;
    };

    struct TGcQueriesStmts::TImpl::TSyncTotalSizeRemovalIterator: public TGcQueriesStmts::TImpl::TAsyncRemovalIterator {
        TSyncTotalSizeRemovalIterator(TImpl& parent, NACCache::TCancelCallback& callback)
            : TAsyncRemovalIterator(parent, callback)
        {
        }

        bool StopInnerCondition() const noexcept {
            // Ignore minor races here, do not acquire Callback.LockWaitFlag
            return Callback.IsLimitReached() || Callback.IsShutdownPending();
        }

        // Next method for outer loop
        bool NextOuter() {
            if (Callback.IsShutdownPending()) {
                return false;
            }

            return NextOuterGraceful();
        }
    };

    struct TGcQueriesStmts::TImpl::TSyncOldItemsRemovalIterator {
        TSyncOldItemsRemovalIterator(TImpl& parent, NACCache::TCancelCallback& callback, i64 ageLimit)
            : Parent(parent)
            , Callback(callback)
            , AgeLimit(ageLimit)
        {
        }

        void InitInner() {
            LastAccessTimeInner = LastAccessTimeOuter;
            Parent.Get(GCOldNext).Reset();
            Parent.Get(GCOldNext).Bind("@PrevLastAccessTime", LastAccessTimeInner).Bind("@AgeLimit", AgeLimit);
            LastStepResult = false;
            RemovedCount = 0;
        }

        // Next method for inner loop
        bool NextInner() {
            for (; RemovedCount < MaxRemoveCount && (LastStepResult = Parent.Get(GCOldNext).Step());) {
                AcRowid = Parent.GetColumnInt64(GCOldNext, 0, "AcsRef");
                LastAccessTimeInner = Parent.GetColumnInt64(GCOldNext, 1, "LastAccessTime");
                i64 reqCount = Parent.GetColumnInt64(GCOldNext, 2, "RequestCount");
                if (reqCount > 0) {
                    continue;
                }
                ++RemovedCount;
                return true;
            }
            return false;
        }

        i64 Get() const noexcept {
            return AcRowid;
        }

        bool StopInnerCondition() const noexcept {
            // Ignore minor races here, do not acquire Callback.LockWaitFlag
            return Callback.IsShutdownPending();
        }

        // Next method for outer loop
        bool NextOuter() {
            if (Callback.IsShutdownPending()) {
                return false;
            }
            return NextOuterGraceful();
        }

        // Next method for outer loop
        bool NextOuterGraceful() {
            if (!LastStepResult && LastAccessTimeOuter == 0) {
                // Loop started with first entry and exited when there is no data
                return false;
            }

            LastAccessTimeOuter = LastStepResult ? LastAccessTimeInner : (i64)0;
            return true;
        }

        TImpl& Parent;
        NACCache::TCancelCallback& Callback;

        i64 LastAccessTimeInner = 0;
        i64 LastAccessTimeOuter = 0;
        const i64 AgeLimit;
        int RemovedCount = 0;
        bool LastStepResult = false;
        i64 AcRowid = -1;

        constexpr static int MaxRemoveCount = 500;
    };

    struct TGcQueriesStmts::TImpl::TSyncBigItemsRemovalIterator {
        TSyncBigItemsRemovalIterator(TImpl& parent, NACCache::TCancelCallback& callback, i64 blobSizeLimit)
            : Parent(parent)
            , Callback(callback)
            , BlobSizeLimit(blobSizeLimit)
        {
        }

        void InitInner() {
            LastBlobInner = LastBlobOuter;
            Parent.Get(GCBigBlobs).Reset();
            Parent.Get(GCBigBlobs).Bind("@PrevBlobId", LastBlobOuter);
            LastBlobStepResult = false;
            LastAcStepResult = false;
            RemovedCount = 0;
        }

        // Next method for inner loop
        bool NextInner() {
            while (LastAcStepResult || (LastBlobStepResult = Parent.Get(GCBigBlobs).Step())) {
                LastBlobInner = Parent.GetColumnInt64(GCBigBlobs, 0, "BlobRef");
                auto fsSize = Parent.GetColumnInt64(GCBigBlobs, 1, "FSSize");
                if (fsSize < BlobSizeLimit) {
                    continue;
                }
                if (!LastAcStepResult) {
                    Parent.Get(GCBigAcs).Reset();
                    Parent.Get(GCBigAcs).Bind("@BlobRef", LastBlobInner);
                }
                for (; RemovedCount < MaxRemoveCount && (LastAcStepResult = Parent.Get(GCBigAcs).Step());) {
                    AcRowid = Parent.GetColumnInt64(GCBigAcs, 0, "AcsRef");
                    Parent.Get(GCAcReqCount).Reset();
                    Parent.Get(GCAcReqCount).Bind("@AcsRef", AcRowid);
                    if (!Parent.Get(GCAcReqCount).Step()) {
                        continue;
                    }
                    i64 reqCount = Parent.GetColumnInt64(GCAcReqCount, 0, "RequestCount");
                    if (reqCount > 0) {
                        continue;
                    }
                    ++RemovedCount;
                    return true;
                }
            }
            return false;
        }

        i64 Get() const noexcept {
            return AcRowid;
        }

        bool StopInnerCondition() const noexcept {
            // Ignore minor races here, do not acquire Callback.LockWaitFlag
            return Callback.IsShutdownPending();
        }

        // Next method for outer loop
        bool NextOuter() {
            if (Callback.IsShutdownPending()) {
                return false;
            }
            return NextOuterGraceful();
        }

        // Next method for outer loop
        bool NextOuterGraceful() {
            if (!LastBlobStepResult && LastBlobOuter == 0) {
                // Loop started with first entry and exited when there is no data
                return false;
            }

            LastBlobOuter = LastBlobStepResult ? LastBlobInner : (i64)0;
            return true;
        }

        TImpl& Parent;
        NACCache::TCancelCallback& Callback;

        i64 LastBlobInner = 0;
        i64 LastBlobOuter = 0;
        const i64 BlobSizeLimit;
        int RemovedCount = 0;
        bool LastBlobStepResult = false;
        bool LastAcStepResult = false;
        i64 AcRowid = -1;

        constexpr static int MaxRemoveCount = 500;
    };

    TGcQueriesStmts::TImpl::TImpl(TSQLiteDB& db)
        : TBase(db, 1, GetACDBMutex())
    {
        const char* resources[] = {"ac/gc_queries"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &GC_QUERIES_NAMES, db);

        Y_ABORT_UNLESS(Get(DeleteDeadTask).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(DeleteReq).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GCNextAny).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GCOldNext).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(GCBigBlobs).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GCBigAcs).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GCAcReqCount).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(SelectDeadTask).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(SelectUsedAcs).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(SelectReqCount).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(UpdateIsInUse).BoundParameterCount() == 2);
    }

    void TGcQueriesStmts::TImpl::UpdateStats(const TACStmts::TReturn& out, NACCache::TCancelCallback* callback) {
        AtomicAdd(*callback->GCState.TotalFSSizeInOut, out.TotalFSSizeDiff);
        AtomicAdd(*callback->GCState.TotalSizeInOut, out.TotalSizeDiff);
        AtomicAdd(*callback->GCState.TotalACsInOut, out.ACsDiff);
        AtomicAdd(*callback->GCState.TotalBlobsInOut, out.BlobDiff);

        callback->GCState.DiskFSSizeDiffOut += out.TotalFSSizeDiff;
        callback->GCState.DiskSizeDiffOut += out.TotalSizeDiff;
    }

#define WAIT_FOR_MAIN_THREAD()                                                                  \
    {                                                                                           \
        std::unique_lock<std::mutex> lock(callback->LockWaitFlag);                              \
        callback->WaitFlag.wait(lock, [callback] { return callback->IsWakeUpConditionMet(); }); \
        if (AtomicGet(callback->ShutdownSignaled)) {                                            \
            return;                                                                             \
        }                                                                                       \
    }

    void TGcQueriesStmts::TImpl::CleanSomething(NACCache::TCancelCallback* callback, TACStmts& eraser, TCASManager& cas) {
        TAsyncRemovalIterator iter(*this, *callback);
        WAIT_FOR_MAIN_THREAD();
        CleanSomethingWithIter(eraser, cas, iter);
    }

    void TGcQueriesStmts::TImpl::SynchronousGC(const NACCache::TSyncronousGC& config, NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas) {
        // Synchronous request count handling is done in dbbe.
        switch (config.GetConfigCase()) {
            case NACCache::TSyncronousGC::kTimestamp: {
                LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Time stamp: " << config.GetTimestamp() << Endl;
                TSyncOldItemsRemovalIterator iter(*this, *callback, config.GetTimestamp());
                // Ignored
                // callback->ResetGC(0, 0);
                CleanSomethingWithIter(eraser, cas, iter);
                break;
            }
            case NACCache::TSyncronousGC::kTotalSize: {
                LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Total size: " << config.GetTotalSize() << Endl;
                TSyncTotalSizeRemovalIterator iter(*this, *callback);
                callback->ResetGC(config.GetTotalSize(), 0);
                CleanSomethingWithIter(eraser, cas, iter);
                break;
            }
            case NACCache::TSyncronousGC::kBLobSize: {
                LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Blob size: " << config.GetBLobSize() << Endl;
                TSyncBigItemsRemovalIterator iter(*this, *callback, config.GetBLobSize());
                // Ignored
                // callback->ResetGC(0, 0);
                CleanSomethingWithIter(eraser, cas, iter);
                break;
            }
            default:
                LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACGC]") << "Unknown config: " << (int)config.GetConfigCase() << Endl;
                break;
        }
    }

    template <typename Iterator>
    void TGcQueriesStmts::TImpl::CleanSomethingWithIter(TACStmts& eraser, TCASManager& cas, Iterator& iter) {
        // Avoid peak durations for commit, flatten load, make sure graceful shutdown is postponed
        // till the end of GC.
        while (true) {
            auto casGuard = cas.GetRollbackGuard("GC", true);

            TBase::WrapInTransactionVoid([this, &eraser, &cas, &casGuard, &iter]() -> void {
                THolder<TSelf, TStmtResetter<true>> resetter(this);

                for (iter.InitInner(); iter.NextInner();) {
                    TACStmts::TReturn out = eraser.RemoveUidNested(iter.Get(), cas, casGuard);
                    UpdateStats(out, &iter.Callback);

                    if (iter.StopInnerCondition()) {
                        break;
                    }
                }
            });

            AtomicSet(*iter.Callback.GCState.LastAccessTime, MilliSeconds());
            casGuard.Commit();
            if (!iter.NextOuter()) {
                return;
            }
        }
    }

    void TGcQueriesStmts::TImpl::UpdateAcsRefCounts(NACCache::TCancelCallback* callback) {
        WAIT_FOR_MAIN_THREAD();

        // Avoid peak durations for commit, flatten load.
        while (true) {
            TBase::WrapInTransactionVoid([this, callback]() -> void {
                THolder<TSelf, TStmtResetter<true>> resetter(this);
                constexpr int maxCount = 500;
                int count = 0;

                while (Get(SelectDeadTask).Step()) {
                    auto taskRef = GetColumnInt64(SelectDeadTask, 0, "TaskRef");
                    for (Get(SelectUsedAcs).Bind("@TaskRef", taskRef); Get(SelectUsedAcs).Step(); ++count) {
                        auto resourceId = GetColumnInt64(SelectUsedAcs, 0, "AcsRef");
                        auto reqRowid = GetColumnInt64(SelectUsedAcs, 1, "ReqRowid");

                        Get(SelectReqCount).Bind("@AcsRef", resourceId).Step();
                        auto refCount = GetColumnInt64(SelectReqCount, 0, "RequestCount");
                        Get(SelectReqCount).Execute();
                        Get(UpdateIsInUse).Bind("@AcsRef", resourceId).Bind("@RequestCount", refCount - 1).Execute();
                        Get(DeleteReq).Bind("@ReqRowid", reqRowid).Execute();

                        if (count >= maxCount) {
                            // Commit updates
                            return;
                        }
                        if (callback->IsCancellationPending()) {
                            return;
                        }
                    }
                    Get(DeleteDeadTask).Bind("@TaskRef", taskRef).Execute();
                }
                AtomicSet(callback->RCState.CompletedOut, 1);
            });
            if (callback->IsCancellationPending() || AtomicGet(callback->RCState.CompletedOut)) {
                return;
            }
        }
    }

    TGcQueriesStmts::TGcQueriesStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TGcQueriesStmts::~TGcQueriesStmts() {
    }

    void TGcQueriesStmts::CleanSomething(NACCache::TCancelCallback* callback, TACStmts& eraser, TCASManager& cas) {
        Ref_->CleanSomething(callback, eraser, cas);
    }

    void TGcQueriesStmts::UpdateAcsRefCounts(NACCache::TCancelCallback* callback) {
        Ref_->UpdateAcsRefCounts(callback);
    }

    void TGcQueriesStmts::SynchronousGC(const NACCache::TSyncronousGC& config, NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas) {
        Ref_->SynchronousGC(config, callback, eraser, cas);
    }

}
