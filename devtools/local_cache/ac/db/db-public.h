#pragma once

#include "devtools/local_cache/ac/proto/ac.pb.h"
#include "devtools/local_cache/ac/fs/fs_blobs.h"
#include "devtools/local_cache/common/db-utils/dbi.h"

#include "devtools/local_cache/common/db-running-procs/db-public.h"

#include <devtools/local_cache/ac/db-dbbei.h>

#include <library/cpp/sqlite3/sqlite.h>

#include <util/generic/list.h>
#include <util/generic/maybe.h>

namespace NACCache {
    class TCASManager;
    class TRollbackHandler;
}

namespace NACCachePrivate {
    constexpr ::NCachesPrivate::EIncrementalTaskErasure ProcCleanupMode = ::NCachesPrivate::IncrementalTaskErasure;

    class TCreateTablesStmt : TNonCopyable {
    public:
        TCreateTablesStmt(NSQLite::TSQLiteDB& db)
            : Tables_(db, TVector<TStringBuf>({TStringBuf("ac/db")}))
            , TablesProcs_(db)
            , Views_(db, TVector<TStringBuf>({TStringBuf("ac/views")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Tables_;
        ::NCachesPrivate::TCreateProcTablesStmt<ProcCleanupMode> TablesProcs_;
        ::NCachesPrivate::TStmtSeq Views_;
    };

    class TDropTablesStmt : TNonCopyable {
    public:
        TDropTablesStmt(NSQLite::TSQLiteDB& db)
            : DropDB_(db, TVector<TStringBuf>({TStringBuf("ac/db_drop")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq DropDB_;
    };

    class TSetupConnection : TNonCopyable {
    public:
        TSetupConnection(NSQLite::TSQLiteDB& db, bool enableForeignKeys)
            : Setup_(db, TVector<TStringBuf>({enableForeignKeys ? TStringBuf("ac/foreign_keys_on") : TStringBuf("ac/foreign_keys_off"),
                                              TStringBuf("ac/setup_con")})) {
        }

    private:
        ::NCachesPrivate::TStmtSeq Setup_;
    };

    class TValidateTablesStmt : TNonCopyable {
    public:
        TValidateTablesStmt(NSQLite::TSQLiteDB& db)
            : Validate_(db, TVector<TStringBuf>({TStringBuf("ac/validate")}))
        {
        }

    private:
        ::NCachesPrivate::TStmtSeq Validate_;
    };

    inline NACCache::EOptim Meet(NACCache::EOptim a, NACCache::EOptim b) {
        return static_cast<NACCache::EOptim>(Max(static_cast<int>(a), static_cast<int>(b)));
    }

    /// Main interface to process blobs table.
    /// See TCASManager in cas/cas.h
    class TCASStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            GetBlobChunk,
            GetBlobRowid,
            InsertBlob,
            GetBlobData,
            UpdateRefCount,
            RemoveBlobData,
            Last = RemoveBlobData
        };
        using TEnum = EStmtName;

        using TListOut = TList<NACCache::THash>;

        struct TReturn {
            i64 Rowid;
            // Before and after transaction
            std::pair<i32, i32> RefCount;
            // Exists before and after transaction
            std::pair<bool, bool> Exists;
            ssize_t TotalSizeDiff = 0;
            ssize_t TotalFSSizeDiff = 0;
            NACCache::EOptim CopyMode = NACCache::Copy;
        };

        /// \arg nested If true then wrap into transaction, otherwise rely on caller
        TCASStmts(NSQLite::TSQLiteDB& db, bool nested = false);
        ~TCASStmts();

        // TODO: implement DataRemoved and use params.
        TReturn PutBlob(const NACCache::TFsBlobProcessor::TParams& params, NACCache::TFsBlobProcessor& processor, i32 refCountAdj, TLog& log);
        TReturn GetBlob(NACCache::TFsBlobProcessor& processor, TLog& log);

        /// \param out list with hashes from DB with ROWID > startingRowid
        /// \return max rowid in out
        i64 GetNextChunk(TListOut& out, i64 startingRowid);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    /// Main interface to process acs and acs_blob tables.
    class TACStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            GetACRowid,
            UpdateAC,
            InsertIntoAC,
            SelectFromReqs,
            UpdateRequestCount,
            InsertIntoReqs,
            DeleteFromReqs,
            GetBlobUid,
            InsertIntoACBlobs,
            InsertIntoACGC,
            UpdateIntoACGC,
            GetBlobRefs,
            DeleteAcsBlob,
            DeleteDepsFrom,
            DeleteDepsTo,
            DeleteRequest,
            DeleteAcsGc,
            RemoveAC,
            GetACOriginAndUse,
            GCPutEdge,
            GCSetNumDeps,
            Last = GCSetNumDeps
        };
        using TEnum = EStmtName;

        struct TReturn {
            NACCache::TOrigin Origin;
            i64 ProcId = -1;
            ssize_t TotalSizeDiff = 0;
            ssize_t TotalFSSizeDiff = 0;
            NACCache::EOptim CopyMode = NACCache::Copy;
            int ACsDiff = 0;
            int BlobDiff = 0;
            bool Success;
        };

        TACStmts(NSQLite::TSQLiteDB& db);
        ~TACStmts();

        /// \param cas is wrapper for TCASStmts
        ///
        /// Validation is done in caller:
        ///     - TPutUid's RootPath should be absolute and parent directory of absolute paths in TPutUid's BlobInfo.
        TReturn PutUid(const NACCache::TPutUid&, NACCache::TCASManager& cas, i64 accessCnt);

        /// \param cas is wrapper for TCASStmts
        ///
        /// Validation is done in caller:
        ///     - TGetUid's DestPath should be absolute and point to directory.
        ///     - TGetUid Optimization should NOT be Rename (at this moment).
        TReturn GetUid(const NACCache::TGetUid&, NACCache::TCASManager& cas, i64 accessCnt);

        /// \param cas is wrapper for TCASStmts
        TReturn RemoveUid(const NACCache::TRemoveUid&, NACCache::TCASManager& cas);

        /// \param cas is wrapper for TCASStmts
        ///
        /// if mode == removeIfUnused then used uid is not removed.
        TReturn RemoveUidNested(i64 acRowid, NACCache::TCASManager& cas, NACCache::TRollbackHandler& casGuard);

        TReturn HasUid(const NACCache::THasUid&, i64 accessCnt);

        /// Graph dependencies
        bool PutDeps(const NACCache::TNodeDependencies& deps);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    class TGcQueriesStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            GCNextAny = 0,
            GCOldNext,
            GCBigBlobs,
            GCBigAcs,
            GCAcReqCount,

            SelectDeadTask,
            DeleteDeadTask,
            SelectUsedAcs,
            DeleteReq,
            SelectReqCount,
            UpdateIsInUse,
            Last = UpdateIsInUse
        };
        using TEnum = EStmtName;

        TGcQueriesStmts(NSQLite::TSQLiteDB& db);
        ~TGcQueriesStmts();

        void CleanSomething(NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas);

        void UpdateAcsRefCounts(NACCache::TCancelCallback* callback);

        void SynchronousGC(const NACCache::TSyncronousGC& config, NACCache::TCancelCallback* callback, TACStmts& eraser, NACCache::TCASManager& cas);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    class TStatQueriesStmts : TNonCopyable {
    public:
        /// Part of implementation, put here to use serialization
        enum EStmtName {
            LastAccessNumber,
            TotalDiskSize,
            PageSize,
            PageCount,
            BlobsCount,
            ACsCount,
            ProcsCount,
            GetRunningId,
            GetTaskRef,
            TaskDiskSize,
            AnalyzeDisk,
            Last = AnalyzeDisk
        };
        using TEnum = EStmtName;

        TStatQueriesStmts(NSQLite::TSQLiteDB& db);
        ~TStatQueriesStmts();

        NACCache::TStatus GetStatistics();

        /// max of last_access from acs_gc.
        int GetLastAccessNumber();

        /// returns some stats for peer.
        void GetTaskStats(const NUserService::TPeer& peer, NACCache::TTaskStatus* out);

        /// Provide cache usage summary
        void AnalyzeDU(NACCache::TDiskUsageSummary* out);

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };

    // Helper class to update acs table on removal of task from DB
    class TAcsUpdaterOnTaskRemoval;

    class TRunningQueriesStmts: public NCachesPrivate::TRunningQueriesStmts<TAcsUpdaterOnTaskRemoval, bool, ProcCleanupMode> {
    public:
        TRunningQueriesStmts(NSQLite::TSQLiteDB& db);
    };
}
