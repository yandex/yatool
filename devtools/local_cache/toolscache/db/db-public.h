#pragma once

#include "devtools/local_cache/toolscache/proto/tools.pb.h"
#include "devtools/local_cache/psingleton/systemptr.h"
#include "devtools/local_cache/common/db-utils/dbi.h"

#include "devtools/local_cache/common/db-running-procs/db-public.h"

#include <library/cpp/containers/stack_vector/stack_vec.h>
#include <library/cpp/sqlite3/sqlite.h>
#include <library/cpp/threading/future/future.h>

#include <util/folder/path.h>
#include <util/generic/function.h>
#include <util/generic/maybe.h>
#include <util/thread/pool.h>

namespace NToolsCachePrivate {
    // See db.sql
    enum EToolsSpecial {
        /// For example, multi-file service where atomic unlink is hard.
        DoNotRemove = 0,
        /// Stale: downloaded w/o notification
        Stale = 1,
        /// Known service, see known_service.proto.
        KnownService = 2,
        /// Tool + regular resource
        ToolAndResource = 3,
        /// Regular resource
        Resource = 4,
        /// Tool
        Tool = 5
    };

    // The best approximation for a and b
    inline EToolsSpecial Meet(EToolsSpecial a, EToolsSpecial b) {
        if (a == Stale) {
            return b;
        }
        if (b == Stale) {
            return a;
        }
        return Min(a, b);
    }

    class TCreateTablesStmt : TNonCopyable {
    public:
        TCreateTablesStmt(NSQLite::TSQLiteDB& db)
            : Tables_(db, TVector<TStringBuf>({TStringBuf("tc/db")}))
            , TablesProcs_(db)
            , Views_(db, TVector<TStringBuf>({TStringBuf("tc/views")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Tables_;
        ::NCachesPrivate::TCreateProcTablesStmt<> TablesProcs_;
        ::NCachesPrivate::TStmtSeq Views_;
    };

    class TDropTablesStmt : TNonCopyable {
    public:
        TDropTablesStmt(NSQLite::TSQLiteDB& db)
            : DropDB_(db, TVector<TStringBuf>({TStringBuf("tc/db_drop")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq DropDB_;
    };

    class TSetupConnection : TNonCopyable {
    public:
        TSetupConnection(NSQLite::TSQLiteDB& db)
            : Setup_(db, TVector<TStringBuf>({TStringBuf("tc/setup_con")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Setup_;
    };

    class TValidateTablesStmt : TNonCopyable {
    public:
        TValidateTablesStmt(NSQLite::TSQLiteDB& db)
            : Validate_(db, TVector<TStringBuf>({TStringBuf("tc/validate")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Validate_;
    };

    /// Main interface to insert to DB: Notify(TResourceUsed)
    class TNotifyStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            // Transaction to insert to requests, running, tools.
            // Utilities::Begin*
            GetToolId,
            UpdateTools,
            InsertToTools,
            GetToolIdAgain,

            InsertRequests,

            // Part related to services
            GetService,
            InsertToServices,
            SelectOtherUsingTask,
            UpdateIsInUse,
            UpdateServices,

            // Part related to toolsgc
            GetToolRef,
            InsertForGC,
            UpdateForGC,

            EmergencyGC,
            // Utilities::Commit
            Last = EmergencyGC
        };
        using TEnum = EStmtName;

        using TVecOut = TStackVec<NToolsCache::TSBResource, 64>;

        TNotifyStmts(NSQLite::TSQLiteDB& db);
        ~TNotifyStmts();

        /// accessCnt is a value for toolsgc.last_access
        /// Returns ids from 'tools', 'running' and known disk space consumed for resource.
        std::tuple<i64, i64, size_t> UpdateResourceUsage(i64 accessCnt, const NToolsCache::TResourceUsed&, bool refill, TVecOut& emergencyGC);

        /// accessCnt is a value for toolsgc.last_access
        /// Returns ids from 'tools', 'running' and known disk space consumed for resource.
        std::tuple<i64, i64, size_t> UpdateServiceUsage(i64 accessCnt, const NToolsCache::TServiceStarted&, bool refill, TVecOut& emergencyGC, NToolsCache::TServiceInfo*);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    /// LockResource(TSBResource)
    class TLockResourceStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            LockResourceStmt,
            UnlockResourceStmt,
            UnlockAllResourcesStmt,

            Last = UnlockAllResourcesStmt
        };
        using TEnum = EStmtName;

        using TVecOut = TStackVec<NToolsCache::TSBResource, 64>;

        TLockResourceStmts(NSQLite::TSQLiteDB& db);
        ~TLockResourceStmts();

        /// Locks resource almost forever.
        void LockResource(const NToolsCache::TSBResource&);

        /// Unlocks resource, mark as both tool and resource.
        void UnlockSBResource(const NToolsCache::TSBResource&);

        /// Unlock all resources marked with special == 0
        void UnlockAllResources();

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    enum EKeepDir {
        KeepIfUseful,
        RemoveUnconditionally
    };

    class TFsModStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            // Remove tool dir
            // Utilities::Begin
            DeleteUnsafe,
            // Removal on FS in between
            // Utilities::Commit

            // Remove tool dir
            // Utilities::Begin
            CheckSafe,
            DeleteSafe,
            // Removal on FS in between
            // Utilities::Commit
            Last = DeleteSafe
        };
        using TEnum = EStmtName;

        TFsModStmts(NSQLite::TSQLiteDB& db, IThreadPool& pool);
        ~TFsModStmts();

        /// Unsafe delete directory.
        NThreading::TFuture<NToolsCache::TSBResource> UnsafeDeleteDir(const NToolsCache::TSBResource&);

        /// Safe delete directory.
        /// Needs rowid. Returns parameters of removed resource.
        std::pair<NThreading::TFuture<NToolsCache::TSBResource>, bool> SafeDeleteDir(i64, EKeepDir mode);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    class TFsFillerStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            // Get a tools w/o total size computed, that can be GC'ed
            GetChunk = 0,
            // Get all tools w/o total size computed
            GetChunkAll,
            // Update total size for a tool.
            UpdateSize,

            // Synchronize with FS.
            // Utilities::Begin
            QueryExisting,
            InsertDiscovered,
            // Utilities::Commit

            Last = InsertDiscovered
        };
        using TEnum = EStmtName;

        /// pair.first is a rowid for SetComputedSize
        using TVecOut = TStackVec<std::pair<i64, NToolsCache::TSBResource>, 64>;

        TFsFillerStmts(NSQLite::TSQLiteDB& db);
        ~TFsFillerStmts();

        /// Get a chunk to compute disk usage.
        /// (path, sbid)
        /// \a all means not only tracked by gc
        TVecOut GetDirsToComputeSize(bool all);
        /// Update disk usage: rowid, disk_size.
        /// (rowid, size)
        void SetComputedSize(i64 rowid, size_t diskSize);

        /// Insert discovered directory EToolsSpecial::Stale
        void InsertDiscoveredDirs(TFsPath path, const TVector<i64>& discovered);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    class TGcQueriesStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            GCNext = 0,
            GCStale,
            Last = GCStale
        };
        using TEnum = EStmtName;

        TGcQueriesStmts(NSQLite::TSQLiteDB& db);
        ~TGcQueriesStmts();

        /// ToolId to clean and its diskSize.
        TMaybe<std::pair<i64, size_t>> GetSomethingToClean();

        /// ToolId to clean.
        TMaybe<i64> GetStaleToClean();

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    class TStatQueriesStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            TotalSize = 0,
            TotalSizeLocked,
            NotDiscovered,
            LastAccessNumber,
            PageSize,
            PageCount,
            ToolsCount,
            ProcsCount,
            GetRunningId,
            GetTaskRef,
            TaskDiskSize,
            TaskNonComputed,
            TaskLocked,
            Last = TaskLocked
        };
        using TEnum = EStmtName;

        TStatQueriesStmts(NSQLite::TSQLiteDB& db);
        ~TStatQueriesStmts();

        NToolsCache::TStatus GetStatistics();

        /// max of last_access from toolsgc.
        int GetLastAccessNumber();

        /// returns some stats for peer.
        void GetTaskStats(const NUserService::TPeer& peer, NToolsCache::TTaskStatus* out);

        /// returns some stats for taskRef.
        /// Helper function to call from other transactions.
        void GetTaskStats(i64 taskRef, NToolsCache::TTaskStatus* out);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    // Helper class to update tools table on removal of task from DB
    class TToolsUpdaterOnTaskRemoval;

    class TRunningQueriesStmts: public NCachesPrivate::TRunningQueriesStmts<TToolsUpdaterOnTaskRemoval, NToolsCache::TTaskStatus> {
    public:
        enum EStmtName {
            SelectUsedTools,
            SelectOtherUsingTask,
            SelectUsingService,
            UpdateIsInUse,
            Last = UpdateIsInUse
        };
        using TEnum = EStmtName;

        TRunningQueriesStmts(NSQLite::TSQLiteDB& db);
    };

    class TServiceVersionQuery : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            GetService,
            Last = GetService
        };
        using TEnum = EStmtName;

        TServiceVersionQuery(NSQLite::TSQLiteDB& db);
        ~TServiceVersionQuery();

        int GetServiceVersion(const TString& serviceName);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };
}
