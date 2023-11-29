#include "db-private.h"

#include "devtools/local_cache/toolscache/fs/fs_ops.h"

#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/string/cast.h>
#include <util/string/split.h>
#include <util/string/strip.h>
#include <util/system/datetime.h>

#include "devtools/local_cache/common/db-running-procs/db_running_impl.h"

namespace NToolsCachePrivate {
    using namespace NToolsCache;

    struct TToolsCacheDBSingleton {
        TRWMutex DBMutex;
    };

    static TRWMutex& GetToolsDBMutex() {
        return Singleton<TToolsCacheDBSingleton>()->DBMutex;
    }
}

namespace {
    static const char* NOTIFY_QUERIES_NAMES[] = {
        "@Bottle",
        // Env of service
        "@EnvCwdArgs",
        // Result of GetTaskId
        "@GetTaskId",
        // Result of GetToolRowid
        "@GetToolId",
        // Result of GetToolRowid
        "@GetToolIdSpecialAcc",
        "@LastAccess",
        "@LastAccessTime",
        // Service name
        "@Name",
        "@Path",
        "@Pattern",
        // Existing service
        "@Rowid",
        "@Sb",
        // Tool of existing service
        "@ToolRef",
        // Service version
        "@Version"};

    static const char* LOCK_QUERIES_NAMES[] = {
        "@Path",
        "@Sb"};
}

/// TNotifyStmts and TLockResourceStmts implementations.
namespace NToolsCachePrivate {
    TInsertRunningStmts::TInsertRunningStmts(TSQLiteDB& db)
        : NCachesPrivate::TInsertRunningStmts(db)
    {
    }

    TNotifyStmts::TImpl::TImpl(TSQLiteDB& db)
        // Infinite number of retries
        : TBase(db, size_t(-1), GetToolsDBMutex())
        , ProcInserter_(db)
    {
        const char* resources[] = {"tc/notify"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &NOTIFY_QUERIES_NAMES, db);
        Y_ABORT_UNLESS(Get(GetService).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GetToolId).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(GetToolIdAgain).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(GetToolRef).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(InsertForGC).BoundParameterCount() == 5);
        Y_ABORT_UNLESS(Get(InsertRequests).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(InsertToServices).BoundParameterCount() == 4);
        Y_ABORT_UNLESS(Get(InsertToTools).BoundParameterCount() == 3);
        Y_ABORT_UNLESS(Get(SelectOtherUsingTask).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(UpdateForGC).BoundParameterCount() == 5);
        Y_ABORT_UNLESS(Get(UpdateIsInUse).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(UpdateServices).BoundParameterCount() == 4);
        Y_ABORT_UNLESS(Get(UpdateTools).BoundParameterCount() == 2);
    }

    // Strings from msg are passed by reference to bindings.
    void TNotifyStmts::TImpl::BindStmts(i64 accessCnt, const NToolsCache::TSBResource& resource) {
        // Prepare stage for the first transaction.
        {
            Get(GetToolId).Bind("@Sb", resource.GetSBId()).Bind("@Path", resource.GetPath());

            // UpdateTools needs @GetToolIdSpecialAcc
            // UpdateTools needs @GetToolId

            Get(InsertToTools).Bind("@Sb", resource.GetSBId()).Bind("@Path", resource.GetPath());

            Get(GetToolIdAgain).Bind("@Sb", resource.GetSBId()).Bind("@Path", resource.GetPath());

            // InsertRequests needs @GetToolId
            // InsertRequests needs @GetTaskId
        }
        // Prepare stage for the GC transaction.
        {
            i64 time = MilliSeconds();
            // GetToolRef needs @GetToolId

            Get(InsertForGC).Bind("@LastAccess", accessCnt).Bind("@LastAccessTime", time);
            // @Pattern and @Bottle are set later.
            // InsertForGC needs @GetToolId

            Get(UpdateForGC).Bind("@LastAccess", accessCnt).Bind("@LastAccessTime", time);
            // @Pattern and @Bottle are set later.
            // UpdateForGC needs @GetToolId
        }
    }

    void TNotifyStmts::TImpl::CollectEmergencyGC(TVecOut& emergencyGC) {
        while (Get(EmergencyGC).Step()) {
            TSBResource res;
            res.SetSBId(ToString(GetColumnText(EmergencyGC, 0, "SbId")));
            res.SetPath(ToString(GetColumnText(EmergencyGC, 1, "Path")));
            emergencyGC.emplace_back(res);
        }
    }

    std::tuple<i64, i64, size_t> TNotifyStmts::TImpl::UpdateTablesForIntegrity(EToolsSpecial special, const NUserService::TPeer& peer) {
        THolder<TSelf, TStmtResetter<false>> resetter(this);

        // Return value.
        i64 toolId = 0;
        size_t diskSize = -1;
        // At most one row
        if (Get(GetToolId).Step()) {
            Y_ABORT_UNLESS(Get(GetToolId).ColumnCount() == 3);
            toolId = GetColumnInt64(GetToolId, 0, "Rowid");
            special = Meet(special, static_cast<EToolsSpecial>(GetColumnInt64(GetToolId, 1, "Special")));
            diskSize = GetColumnInt64(GetToolId, 2, "DiskSize");
            Y_ABORT_UNLESS(!Get(GetToolId).Step());

            Get(UpdateTools).Bind("@GetToolId", toolId).Bind("@GetToolIdSpecialAcc", special).Execute();
        } else {
            Get(InsertToTools).Bind("@GetToolIdSpecialAcc", int(special));
            Get(InsertToTools).Execute();
            toolId = GetRowid(GetToolIdAgain);
        }

        i64 taskRef, runningId;
        std::tie(taskRef, runningId) = ProcInserter_.InsertRunningProc(peer);

        Get(InsertRequests).Bind("@GetToolId", toolId).Bind("@GetTaskId", taskRef).Execute();

        return std::make_tuple(toolId, runningId, diskSize);
    }

    void TNotifyStmts::TImpl::UpdateTablesForGC(i64 toolId, const TResourceUsed& msg) {
        THolder<TSelf, TStmtResetter<false>> resetter(this);

        // The following data is accumulated:
        TString pattern(msg.GetPattern());
        TString bottle(msg.GetBottle());
        // Reset pattern and bottle
        THolder<TSQLiteStatement, TClearBindings> resetInsertTask(&Get(UpdateForGC));
        THolder<TSQLiteStatement, TClearBindings> resetGetTaskId(&Get(InsertForGC));

        Get(GetToolRef).Bind("@GetToolId", toolId);
        // At most one row
        if (Get(GetToolRef).Step()) {
            Y_ABORT_UNLESS(Get(GetToolRef).ColumnCount() == 2);
            if (pattern.Empty()) {
                pattern = GetColumnText(GetToolRef, 0, "Pattern");
            }
            if (bottle.Empty()) {
                bottle = GetColumnText(GetToolRef, 1, "Bottle");
            }
            Y_ABORT_UNLESS(!Get(GetToolRef).Step());

            Get(UpdateForGC).Bind("@Pattern", pattern).Bind("@Bottle", bottle).Bind("@GetToolId", toolId).Execute();
        } else {
            Get(InsertForGC).Bind("@Pattern", pattern).Bind("@Bottle", bottle).Bind("@GetToolId", toolId).Execute();
        }
    }

    std::tuple<i64, i64, size_t> TNotifyStmts::TImpl::UpdateResourceUsage(i64 accessCnt, const TResourceUsed& msg, bool refill, TVecOut& emergencyGC) {
        // The following data is accumulated:
        EToolsSpecial special = ToolAndResource;
        if (msg.GetPattern().Empty() != msg.GetBottle().Empty()) {
            special = msg.GetPattern().Empty() ? Tool : Resource;
        }

        return TBase::WrapInTransaction([this, &msg, &emergencyGC, special, accessCnt, refill]() -> std::tuple<i64, i64, size_t> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            BindStmts(accessCnt, msg.GetResource());

            i64 toolId(0), runningId(0);
            size_t diskSize(0);
            std::tie(toolId, runningId, diskSize) = this->UpdateTablesForIntegrity(special, msg.GetPeer());
            // Do not split into 2 transactions.
            this->UpdateTablesForGC(toolId, msg);
            if (refill) {
                this->CollectEmergencyGC(emergencyGC);
            }
            return std::make_tuple(toolId, runningId, diskSize);
        });
    }

    void TNotifyStmts::TImpl::UpdateTablesForServices(i64 toolRowid, const NToolsCache::TServiceStarted& newService, NToolsCache::TServiceInfo* existingService) {
        THolder<TSelf, TStmtResetter<false>> resetter(this);

        // The following data is accumulated:
        TString name(newService.GetService().GetName());
        TString envCwdArgs(newService.GetService().GetEnvCwdArgs());
        i32 version(newService.GetService().GetVersion());

        // Reset 'name'
        THolder<TSQLiteStatement, TClearBindings> resetGet(&Get(GetService));
        THolder<TSQLiteStatement, TClearBindings> resetInsert(&Get(InsertToServices));
        THolder<TSQLiteStatement, TClearBindings> resetUpdate(&Get(UpdateServices));

        Get(GetService).Bind("@Name", name);

        // At most one row
        if (Get(GetService).Step()) {
            Y_ABORT_UNLESS(Get(GetService).ColumnCount() == 6);
            i64 rowid = GetColumnInt64(GetService, 0, "Rowid");
            i32 exVersion = GetColumnInt64(GetService, 1, "Version");

            if (exVersion >= version && !newService.GetForceReplacement()) {
                // No need to update.
                existingService->MutableResource()->SetSBId(ToString(GetColumnText(GetService, 3, "SbId")));
                existingService->MutableResource()->SetPath(ToString(GetColumnText(GetService, 4, "Path")));
                existingService->SetEnvCwdArgs(ToString(GetColumnText(GetService, 5, "EnvCmdArgs")));
                existingService->SetName(newService.GetService().GetName());
                return;
            } else {
                *existingService = newService.GetService();
            }

            i64 oldToolRef = GetColumnInt64(GetService, 2, "OldToolRef");
            if (!Get(SelectOtherUsingTask).Bind("@ToolRef", oldToolRef).Step()) {
                Get(UpdateIsInUse).Bind("@ToolRef", oldToolRef).Execute();
            }

            Get(UpdateServices).Bind("@ToolRef", toolRowid).Bind("@Version", version).Bind("@Rowid", rowid).Bind("@EnvCwdArgs", envCwdArgs).Execute();
        } else {
            Get(InsertToServices).Bind("@ToolRef", toolRowid).Bind("@Name", name).Bind("@Version", version).Bind("@EnvCwdArgs", envCwdArgs).Execute();
        }
    }

    std::tuple<i64, i64, size_t> TNotifyStmts::TImpl::UpdateServiceUsage(i64 accessCnt, const TServiceStarted& msg, bool refill, TVecOut& emergencyGC, NToolsCache::TServiceInfo* existingService) {
        EToolsSpecial special = KnownService;
        auto& peer = msg.GetPeer();

        return TBase::WrapInTransaction([this, &msg, &peer, &emergencyGC, special, existingService, accessCnt, refill]() -> std::tuple<i64, i64, size_t> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            BindStmts(accessCnt, msg.GetService().GetResource());

            i64 toolId(0), runningId(0);
            size_t diskSize(0);
            std::tie(toolId, runningId, diskSize) = this->UpdateTablesForIntegrity(special, peer);
            this->UpdateTablesForServices(toolId, msg, existingService);
            if (refill) {
                this->CollectEmergencyGC(emergencyGC);
            }
            return std::make_tuple(toolId, runningId, diskSize);
        });
    }

    TNotifyStmts::TNotifyStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TNotifyStmts::~TNotifyStmts() {
    }

    std::tuple<i64, i64, size_t> TNotifyStmts::UpdateResourceUsage(i64 accessCnt, const TResourceUsed& msg, bool refill, TVecOut& emergencyGC) {
        return Ref_->UpdateResourceUsage(accessCnt, msg, refill, emergencyGC);
    }

    std::tuple<i64, i64, size_t> TNotifyStmts::UpdateServiceUsage(i64 accessCnt, const TServiceStarted& serv, bool refill, TVecOut& emergencyGC, NToolsCache::TServiceInfo* out) {
        return Ref_->UpdateServiceUsage(accessCnt, serv, refill, emergencyGC, out);
    }

    TLockResourceStmts::TImpl::TImpl(TSQLiteDB& db)
        // Infinite number of retries
        : TBase(db, size_t(-1), GetToolsDBMutex())
    {
        const char* resources[] = {"tc/lock_resource"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &LOCK_QUERIES_NAMES, db);
        Y_ABORT_UNLESS(Get(LockResourceStmt).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(UnlockResourceStmt).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(UnlockAllResourcesStmt).BoundParameterCount() == 0);
    }

    void TLockResourceStmts::TImpl::LockResource(const TSBResource& msg) {
        TBase::WrapInTransactionVoid([this, &msg]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(LockResourceStmt).Bind("@Sb", msg.GetSBId());
            Get(LockResourceStmt).Bind("@Path", msg.GetPath());

            Get(LockResourceStmt).Execute();
        });
    }

    void TLockResourceStmts::TImpl::UnlockSBResource(const TSBResource& msg) {
        TBase::WrapInTransactionVoid([this, &msg]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(UnlockResourceStmt).Bind("@Sb", msg.GetSBId());
            Get(UnlockResourceStmt).Bind("@Path", msg.GetPath());

            Get(UnlockResourceStmt).Execute();
        });
    }

    void TLockResourceStmts::TImpl::UnlockAllResources() {
        TBase::WrapInTransactionVoid([this]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(UnlockAllResourcesStmt).Execute();
        });
    }

    TLockResourceStmts::TLockResourceStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TLockResourceStmts::~TLockResourceStmts() {
    }

    void TLockResourceStmts::LockResource(const TSBResource& sb) {
        Ref_->LockResource(sb);
    }

    void TLockResourceStmts::UnlockSBResource(const TSBResource& sb) {
        Ref_->UnlockSBResource(sb);
    }

    void TLockResourceStmts::UnlockAllResources() {
        Ref_->UnlockAllResources();
    }
}

namespace {
    static const char* FS_FILLER_NAMES[] = {
        "@NewSize",
        "@Path",
        "@Rowid",
        "@Sb"};

    static const char* FS_MOD_NAMES[] = {
        "@Path",
        "@Sb",
        "@ToolRef"};
}

/// TFsFillerStmts and TFsModStmts implementations.
namespace NToolsCachePrivate {
    TFsFillerStmts::TImpl::TImpl(TSQLiteDB& db)
        : TBase(db, 1, GetToolsDBMutex())
    {
        const char* resources[] = {"tc/fs_filler_seq"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &FS_FILLER_NAMES, db);

        Y_ABORT_UNLESS(Get(GetChunk).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(GetChunkAll).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(InsertDiscovered).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(QueryExisting).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(UpdateSize).BoundParameterCount() == 2);
    }

    TFsFillerStmts::TVecOut TFsFillerStmts::TImpl::GetDirsToComputeSize(bool all) {
        TVecOut out;
        TBase::WrapInTransactionVoid([this, &out, all]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            out.clear();

            auto q = all ? GetChunkAll : GetChunk;
            while (Get(q).Step()) {
                TSBResource res;
                res.SetPath(TString(GetColumnText(q, 1, "Path")));
                res.SetSBId(TString(GetColumnText(q, 2, "SbId")));
                out.emplace_back(GetColumnInt64(q, 0, "Rowid"), res);
            }
        });
        return out;
    }

    void TFsFillerStmts::TImpl::SetComputedSize(i64 rowid, size_t diskSize) {
        TBase::WrapInTransactionVoid([this, rowid, diskSize]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(UpdateSize).Bind("@Rowid", rowid).Bind("@NewSize", (i64)diskSize).Execute();
        });
    }

    void TFsFillerStmts::TImpl::InsertDiscoveredDirs(TFsPath path, const TVector<i64>& discovered) {
        TString npath(path);

        TBase::WrapInTransactionVoid([this, &discovered, &npath]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(QueryExisting).Bind("@Path", npath);
            Get(InsertDiscovered).Bind("@Path", npath);

            THashSet<i64> hashDiscovered;
            hashDiscovered.insert(discovered.begin(), discovered.end());
            while (Get(QueryExisting).Step()) {
                hashDiscovered.erase(GetColumnInt64(QueryExisting, 0, "SbId"));
            }
            if (hashDiscovered.empty()) {
                return;
            }
            for (auto sb : hashDiscovered) {
                Get(InsertDiscovered).Bind("@Sb", sb).Execute();
            }
        });
    }

    TFsFillerStmts::TFsFillerStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TFsFillerStmts::~TFsFillerStmts() {
    }

    TFsFillerStmts::TVecOut TFsFillerStmts::GetDirsToComputeSize(bool all) {
        return Ref_->GetDirsToComputeSize(all);
    }

    void TFsFillerStmts::SetComputedSize(i64 rowid, size_t diskSize) {
        Ref_->SetComputedSize(rowid, diskSize);
    }

    void TFsFillerStmts::InsertDiscoveredDirs(TFsPath path, const TVector<i64>& discovered) {
        Ref_->InsertDiscoveredDirs(path, discovered);
    }

    TFsModStmts::TImpl::TImpl(TSQLiteDB& db, IThreadPool& pool)
        : TBase(db, 1, GetToolsDBMutex())
        , Pool_(pool)
    {
        const char* resources[] = {"tc/fs_mod_seq"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &FS_MOD_NAMES, db);
        Y_ABORT_UNLESS(Get(CheckSafe).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(DeleteSafe).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(DeleteUnsafe).BoundParameterCount() == 2);
    }

    std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> TFsModStmts::TImpl::SafeDeleteDir(i64 resourceId, EKeepDir mode) {
        return TBase::WrapInTransaction([this, resourceId, mode]() -> std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(CheckSafe).Bind("@ToolRef", resourceId);
            Get(DeleteSafe).Bind("@ToolRef", resourceId);

            if (Get(CheckSafe).Step()) {
                Y_ABORT_UNLESS(resourceId == GetColumnInt64(CheckSafe, 0, "ToolRef"));
                TSBResource fsResource;
                fsResource.SetPath(ToString(GetColumnText(CheckSafe, 1, "Path")));
                fsResource.SetSBId(ToString(GetColumnText(CheckSafe, 2, "SbId")));

                Y_ABORT_UNLESS(!Get(CheckSafe).Step());
                switch (mode) {
                    case KeepIfUseful: {
                        auto p = RemoveAtomicallyIfBroken(fsResource, false, Pool_);
                        if (p.second) {
                            Get(DeleteSafe).Execute();
                        }
                        return p;
                    }
                    case RemoveUnconditionally:
                        Get(DeleteSafe).Execute();
                        // Do actual FS, relying on successful BEGIN TRANSACTION
                        return std::make_pair(RemoveAtomically(fsResource, false, Pool_), true);
                }
                Y_UNREACHABLE();
            }
            return std::make_pair(NThreading::MakeFuture(TSBResource()), true);
        });
    }

    NThreading::TFuture<NToolsCache::TSBResource> TFsModStmts::TImpl::UnsafeDeleteDir(const TSBResource& resource) {
        return TBase::WrapInTransaction([this, &resource]() -> NThreading::TFuture<NToolsCache::TSBResource> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(DeleteUnsafe).Bind("@Sb", resource.GetSBId()).Bind("@Path", resource.GetPath()).Execute();

            // Do actual FS, relying on successful BEGIN TRANSACTION
            return RemoveAtomically(resource, false, Pool_);
        });
    }

    std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> TFsModStmts::SafeDeleteDir(i64 resourceId, EKeepDir mode) {
        return Ref_->SafeDeleteDir(resourceId, mode);
    }

    NThreading::TFuture<NToolsCache::TSBResource> TFsModStmts::UnsafeDeleteDir(const TSBResource& res) {
        return Ref_->UnsafeDeleteDir(res);
    }

    TFsModStmts::TFsModStmts(TSQLiteDB& db, IThreadPool& pool) {
        Ref_.Reset(new TImpl(db, pool));
    }

    TFsModStmts::~TFsModStmts() {
    }
}

/// TGcQueriesStmts implementation.
namespace NToolsCachePrivate {
    TGcQueriesStmts::TImpl::TImpl(TSQLiteDB& db)
        : TBase(db, 1, GetToolsDBMutex())
    {
        const char* resources[] = {"tc/gc_queries_seq"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, (const char*(*)[1]) nullptr, db);

        Y_ABORT_UNLESS(Get(GCNext).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(GCStale).BoundParameterCount() == 0);
    }

    TMaybe<std::pair<i64, size_t>> TGcQueriesStmts::TImpl::GetSomethingToClean() {
        return TBase::WrapInTransaction([this]() -> TMaybe<std::pair<i64, size_t>> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            TMaybe<std::pair<i64, size_t>> out;
            if (Get(GCNext).Step()) {
                out = std::make_pair(GetColumnInt64(GCNext, 0, "ToolRef"), GetColumnInt64(GCNext, 1, "DiskSize"));
                Y_ABORT_UNLESS(!Get(GCNext).Step());
            }
            return out;
        });
    }

    TMaybe<i64> TGcQueriesStmts::TImpl::GetStaleToClean() {
        return TBase::WrapInTransaction([this]() -> TMaybe<i64> {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            TMaybe<i64> out;
            if (Get(GCStale).Step()) {
                out = GetColumnInt64(GCStale, 0, "ToolRef");
                Y_ABORT_UNLESS(!Get(GCStale).Step());
            }
            return out;
        });
    }

    TGcQueriesStmts::TGcQueriesStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TGcQueriesStmts::~TGcQueriesStmts() {
    }

    TMaybe<std::pair<i64, size_t>> TGcQueriesStmts::GetSomethingToClean() {
        return Ref_->GetSomethingToClean();
    }

    TMaybe<i64> TGcQueriesStmts::GetStaleToClean() {
        return Ref_->GetStaleToClean();
    }
}

namespace {
    static const char* STATS_NAMES[] = {
        "@ProcCTime",
        "@ProcPid",
        "@TaskId",
        "@TaskRef"};
}

/// TStatQueriesStmts implementation.
namespace NToolsCachePrivate {
    TStatQueriesStmts::TImpl::TImpl(TSQLiteDB& db)
        : TBase(db, 1, GetToolsDBMutex())
    {
        const char* resources[] = {"tc/stat_seq"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &STATS_NAMES, db);
        Y_ABORT_UNLESS(Get(GetRunningId).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(LastAccessNumber).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(NotDiscovered).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(PageCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(PageSize).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(ProcsCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(TaskDiskSize).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(TaskLocked).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(TaskNonComputed).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(ToolsCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(TotalSize).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(TotalSizeLocked).BoundParameterCount() == 0);
    }

    TStatus TStatQueriesStmts::TImpl::GetStatistics() {
        return TBase::WrapInTransaction([this]() -> TStatus {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            if (PageSize_ == 0) {
                Get(PageSize).Step();
                // It is pragma.
                PageSize_ = Get(PageSize).ColumnInt64(0);
            }
            TStatus out;
            Y_ABORT_UNLESS(Get(TotalSize).Step());
            out.SetTotalKnownSize(GetColumnInt64(TotalSize, 0, "Sum"));
            Y_ABORT_UNLESS(Get(TotalSizeLocked).Step());
            out.SetTotalKnownSizeLocked(GetColumnInt64(TotalSizeLocked, 0, "Sum"));
            Y_ABORT_UNLESS(Get(NotDiscovered).Step());
            out.SetNonComputedCount(GetColumnInt64(NotDiscovered, 0, "Cnt"));

            Get(PageCount).Step();
            i64 pages = Get(PageCount).ColumnInt64(0);
            out.SetTotalDBSize(PageSize_ * pages);

            Y_ABORT_UNLESS(Get(ToolsCount).Step());
            out.SetToolCount(GetColumnInt64(ToolsCount, 0, "Cnt"));

            Y_ABORT_UNLESS(Get(ProcsCount).Step());
            out.SetProcessesCount(GetColumnInt64(ProcsCount, 0, "Cnt"));

            return out;
        });
    }

    int TStatQueriesStmts::TImpl::GetLastAccessNumber() {
        return TBase::WrapInTransaction([this]() -> int {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Y_ABORT_UNLESS(Get(LastAccessNumber).Step());
            return GetColumnInt64(LastAccessNumber, 0, "Max");
        });
    }

    void TStatQueriesStmts::TImpl::GetTaskStats(i64 taskRef, NToolsCache::TTaskStatus* out) {
        THolder<TSelf, TStmtResetter<true>> resetter(this);

        Get(TaskDiskSize).Bind("@TaskRef", taskRef);
        Get(TaskNonComputed).Bind("@TaskRef", taskRef);
        Get(TaskLocked).Bind("@TaskRef", taskRef);
        if (Get(TaskDiskSize).Step()) {
            Y_ABORT_UNLESS(Get(TaskDiskSize).ColumnCount() == 1);
            out->SetTotalKnownSize(GetColumnInt64(TaskDiskSize, 0, "DiskSize"));
        }
        if (Get(TaskNonComputed).Step()) {
            Y_ABORT_UNLESS(Get(TaskNonComputed).ColumnCount() == 1);
            out->SetNonComputedCount(GetColumnInt64(TaskNonComputed, 0, "NonComputedCount"));
        }
        if (Get(TaskLocked).Step()) {
            Y_ABORT_UNLESS(Get(TaskLocked).ColumnCount() == 2);
            out->SetLockedCount(GetColumnInt64(TaskLocked, 0, "LockedCount"));
            auto sz = GetColumnInt64(TaskLocked, 1, "LockedDiskSize");
            // Do not need too be accurate
            out->SetTotalKnownSizeLocked(sz > 0 ? sz : 0);
        }
    }

    void TStatQueriesStmts::TImpl::GetTaskStats(const NUserService::TPeer& peer, NToolsCache::TTaskStatus* out) {
        TString taskId;

        TBase::WrapInTransactionVoid([this, &peer, &taskId, out]() {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            if (peer.GetTaskGSID().empty()) {
                auto& proc = peer.GetProc();
                Get(GetRunningId).Bind("@ProcPid", (i64)proc.GetPid()).Bind("@ProcCTime", (i64)proc.GetStartTime());
                auto maybeR = GetMaybeRowid(GetRunningId);
                if (!maybeR) {
                    return;
                }
                taskId = ToString(maybeR.GetRef());
            } else {
                taskId = peer.GetTaskGSID();
            }
            Get(GetTaskRef).Bind("@TaskId", taskId);
            auto maybeTask = GetMaybeRowid(GetTaskRef);
            if (!maybeTask) {
                return;
            }
            GetTaskStats(maybeTask.GetRef(), out);
        });
    }

    TStatQueriesStmts::TStatQueriesStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TStatQueriesStmts::~TStatQueriesStmts() {
    }

    TStatus TStatQueriesStmts::GetStatistics() {
        return Ref_->GetStatistics();
    }

    int TStatQueriesStmts::GetLastAccessNumber() {
        return Ref_->GetLastAccessNumber();
    }

    void TStatQueriesStmts::GetTaskStats(const NUserService::TPeer& peer, NToolsCache::TTaskStatus* out) {
        return Ref_->GetTaskStats(peer, out);
    }

    void TStatQueriesStmts::GetTaskStats(i64 taskRef, NToolsCache::TTaskStatus* out) {
        return Ref_->GetTaskStats(taskRef, out);
    }
}

namespace {
    static const char* TOOLS_UPDATE_NAMES[] = {
        "@TaskRef",
        "@ToolRef"};
}

// Instantiate required template
template class NCachesPrivate::TRunningQueriesStmts<NToolsCachePrivate::TToolsUpdaterOnTaskRemoval, NToolsCache::TTaskStatus>;

namespace NToolsCachePrivate {
    TToolsUpdaterOnTaskRemoval::TToolsUpdaterOnTaskRemoval(TSQLiteDB& db)
        : Stats_(db)
    {
        const char* resources[] = {"tc/update_tools"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &TOOLS_UPDATE_NAMES, db);
    }

    void TToolsUpdaterOnTaskRemoval::UpdateResourceOnDeadTask(i64 taskRef) {
        THolder<TToolsUpdaterOnTaskRemoval, TStmtResetter<true>> resetter(this);

        Get(TOuter::SelectUsedTools).Bind("@TaskRef", taskRef);
        while (Get(TOuter::SelectUsedTools).Step()) {
            auto resourceId = GetColumnInt64(TOuter::SelectUsedTools, 0, "ToolRef");

            Get(TOuter::SelectOtherUsingTask).Reset();
            Get(TOuter::SelectOtherUsingTask).Bind("@TaskRef", taskRef).Bind("@ToolRef", resourceId);
            Get(TOuter::SelectUsingService).Reset();
            Get(TOuter::SelectUsingService).Bind("@ToolRef", resourceId);
            if (!Get(TOuter::SelectOtherUsingTask).Step() && !Get(TOuter::SelectUsingService).Step()) {
                Get(TOuter::UpdateIsInUse).Reset();
                Get(TOuter::UpdateIsInUse).Bind("@ToolRef", resourceId).Execute();
            }
        }
    }

    void TToolsUpdaterOnTaskRemoval::ComputeStat(i64 taskRef) {
        THolder<TToolsUpdaterOnTaskRemoval, TStmtResetter<true>> resetter(this);

        TotalDiskSize_.Clear();
        Stats_.GetTaskStats(taskRef, &TotalDiskSize_);
    }

    void TToolsUpdaterOnTaskRemoval::CheckSqlStatements() const {
        Y_ABORT_UNLESS(Get(TOuter::SelectUsedTools).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(TOuter::SelectOtherUsingTask).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(TOuter::SelectUsingService).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(TOuter::UpdateIsInUse).BoundParameterCount() == 1);
    }

    TRunningQueriesStmts::TRunningQueriesStmts(TSQLiteDB& db)
        : ::NCachesPrivate::TRunningQueriesStmts<TToolsUpdaterOnTaskRemoval, TTaskStatus>(db, GetToolsDBMutex())
    {
    }
}

namespace {
    static const char* VERSION_NAMES[] = {"@Name"};
}

/// TServiceVersionQuery implementation.
namespace NToolsCachePrivate {
    TServiceVersionQuery::TImpl::TImpl(TSQLiteDB& db)
        : TBase(db, size_t(-1), GetToolsDBMutex())
    {
        const char* resources[] = {"tc/version"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &VERSION_NAMES, db);
        Y_ABORT_UNLESS(Get(GetService).BoundParameterCount() == 1);
    }

    int TServiceVersionQuery::TImpl::GetServiceVersion(const TString& name) {
        return TBase::WrapInTransaction([this, &name]() -> int {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(GetService).Bind("@Name", name).Execute();

            if (Get(GetService).Step()) {
                return GetColumnInt64(GetService, 0, "Version");
            }
            return 0;
        });
    }

    TServiceVersionQuery::TServiceVersionQuery(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TServiceVersionQuery::~TServiceVersionQuery() {
    }

    int TServiceVersionQuery::GetServiceVersion(const TString& name) {
        return Ref_->GetServiceVersion(name);
    }
}
