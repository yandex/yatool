#include "db-private.h"

#include <library/cpp/logger/global/rty_formater.h>

#include <util/folder/path.h>
#include <util/generic/yexception.h>
#include <util/string/cast.h>
#include <util/system/datetime.h>

#include "devtools/local_cache/common/db-running-procs/db_running_impl.h"

namespace NACCachePrivate {
    using namespace NACCache;

    TRWMutex& GetACDBMutex() {
        return Singleton<TACCacheDBSingleton>()->DBMutex;
    }
}

namespace {
    static const char* CAS_QUERIES_NAMES[] = {
        "@Content",
        "@FSSize",
        "@DBStoreMode",
        "@Mode",
        "@RefCount",
        "@Size",
        "@Rowid",
        "@Uid"};
}

namespace NACCachePrivate {
    TCASStmts::TImpl::TImpl(TSQLiteDB& db, bool nested)
        // Infinite number of retries
        : TBase(db, size_t(-1), GetACDBMutex())
        , Nested_(nested)
    {
        const char* resources[] = {"ac/cas"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &CAS_QUERIES_NAMES, db);
        Y_ABORT_UNLESS(Get(GetBlobChunk).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GetBlobRowid).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(InsertBlob).BoundParameterCount() == 7);
        Y_ABORT_UNLESS(Get(RemoveBlobData).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(UpdateRefCount).BoundParameterCount() == 2);
    }

    TCASStmts::TReturn TCASStmts::TImpl::PutBlob(const TFsBlobProcessor::TParams& params, TFsBlobProcessor& processor, i32 refCountAdj, TLog& log) {
        // Capture temp TString to pass as TStringBuf
        const auto& uidStr = processor.GetUid();
        // Keep it for potential call to Bind, which may have stale TStringBuf on return from
        // lambda.
        TString content;

        auto transaction = [this, &params, &processor, &content, &uidStr, &log, refCountAdj]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            Get(GetBlobRowid).Bind("@Uid", uidStr);
            Get(InsertBlob).Bind("@Uid", uidStr);

            Y_ABORT_UNLESS(params.Mode != DataRemoved);

            i32 oldRefCount = 0;
            i64 rowid = -1;
            bool exists = Get(GetBlobRowid).Step();
            auto copyMode = processor.GetRequestedOptimization();
            EBlobStoreMode storeMode = OnFS;
            if (exists) {
                Y_ABORT_UNLESS(Get(GetBlobRowid).ColumnCount() == 3);

                rowid = GetColumnInt64(GetBlobRowid, 0, "Rowid");
                storeMode = static_cast<EBlobStoreMode>(GetColumnInt64(GetBlobRowid, 1, "DBStoreMode"));
                oldRefCount = GetColumnInt64(GetBlobRowid, 2, "RefCount");
            }

            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACCAS]")
                << "PB: uid=" << uidStr << ", old=" << oldRefCount << ", new=" << oldRefCount + refCountAdj << Endl;

            // oldRefCount >= 0 and -oldRefCount is well-defined.
            if (refCountAdj <= -oldRefCount && !exists) {
                return TReturn({rowid, std::make_pair(0, 0), std::make_pair(false, false), 0, 0, copyMode});
            } else if (refCountAdj > -oldRefCount && exists) {
                i64 sum = (i64)oldRefCount;
                if (refCountAdj) {
                    sum += (i64)refCountAdj;
                    sum = sum > std::numeric_limits<i32>::max() ? std::numeric_limits<i32>::max() : sum;
                    Get(UpdateRefCount).Bind("@Rowid", rowid).Bind("@RefCount", sum).Execute();
                }
                return TReturn({rowid, std::make_pair(oldRefCount, sum), std::make_pair(exists, exists), 0, 0, copyMode});
            } else if (refCountAdj <= -oldRefCount && exists) {
                ssize_t size = 0, fsSize = 0;
                Get(GetBlobData).Bind("@Uid", uidStr);
                Y_ABORT_UNLESS(Get(GetBlobData).Step());
                size = GetColumnInt64(GetBlobData, 2, "Size");
                fsSize = GetColumnInt64(GetBlobData, 3, "FSSize");

                // Remove if refCount reached 0
                TFsBlobProcessor::TParams params{CodecNone, storeMode};
                Get(RemoveBlobData)
                    .Bind("@Rowid", rowid)
                    .Execute();
                TFsBlobProcessor::TFsInfo meta;
                try {
                    processor.RemoveBlob(params, meta);
                } catch (const TIoException& e) {
                    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[ACCAS]")
                        << "PB: exc=" << e.what() << ", uid=" << uidStr << Endl;
                    if (e.Status() != ENOENT) {
                        throw;
                    }
                    meta.FSSize = meta.Size = 0;
                }

                // Single accounting, i.e. avoid errors due to internal disk optimizations: disk
                // usage varies over time.
                //
                // fsSize = params.Mode == OnFS ? meta.FSSize : fsSize;
                // size = params.Mode == OnFS ? meta.Size : size;
                return TReturn({rowid, std::make_pair(oldRefCount, 0), std::make_pair(exists, false), -size, -fsSize, copyMode});
            } else { // (refCountAdj > -oldRefCount && !exists)
                EOptim optim;
                TFsBlobProcessor::TFsInfo meta;
                std::tie(content, optim) = processor.Put(params, meta);
                ssize_t fsSize = params.Mode == OnFS ? meta.FSSize : 0;
                ssize_t size = params.Mode == OnFS ? meta.Size : content.size();
                Get(InsertBlob)
                    .Bind("@DBStoreMode", static_cast<int>(params.Mode))
                    .BindBlob("@Content", content)
                    .Bind("@Size", size)
                    .Bind("@FSSize", fsSize)
                    .Bind("@Mode", meta.Mode)
                    .Bind("@RefCount", refCountAdj)
                    .Execute();
                // Data WAS put into DB.
                Get(GetBlobRowid).Step();
                return TReturn({GetColumnInt64(GetBlobRowid, 0, "Rowid"), std::make_pair(0, refCountAdj), std::make_pair(exists, true), size, fsSize, optim});
            }
        };
        return Nested_ ? transaction() : TBase::WrapInTransaction(transaction);
    }

    TCASStmts::TReturn TCASStmts::TImpl::GetBlob(TFsBlobProcessor& processor, TLog& log) {
        // Capture temp TString to pass as TStringBuf
        const auto& uidStr = processor.GetUid();

        auto transaction = [this, &processor, &uidStr, &log]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            Get(GetBlobData).Bind("@Uid", uidStr);

            if (!Get(GetBlobData).Step()) {
                // No data available.
                LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACCAS]")
                    << "GB: no-uid=" << uidStr << Endl;
                return TReturn({-1, std::make_pair(0, 0), std::make_pair(false, false), NACCache::Copy});
            }
            Y_ABORT_UNLESS(Get(GetBlobData).ColumnCount() == 7);

            auto storeMode = static_cast<EBlobStoreMode>(GetColumnInt64(GetBlobData, 0, "DBStoreMode"));
            TFsBlobProcessor::TParams params{CodecNone, storeMode};
            // Do not copy data around
            TString content(GetColumnBlob(GetBlobData, 1, "Content"));
            auto rowid = GetColumnInt64(GetBlobData, 4, "Rowid");
            i32 oldRefCount = GetColumnInt64(GetBlobData, 5, "RefCount");
            i64 fileMode = GetColumnInt64(GetBlobData, 6, "Mode");

            auto optim = processor.Get(params, content, fileMode);
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACCAS]")
                << "GB: uid=" << uidStr << Endl;
            return TReturn({rowid, std::make_pair(oldRefCount, oldRefCount),
                            std::make_pair(true, true), 0, 0, optim});
        };
        return Nested_ ? transaction() : TBase::WrapInTransaction(transaction);
    }

    i64 TCASStmts::TImpl::GetNextChunk(TListOut& out, i64 startingRowid) {
        auto transaction = [this, &out, startingRowid]() -> i64 {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            Get(GetBlobChunk).Bind("@Rowid", startingRowid);

            i64 maxRowid = startingRowid + 1;
            out.clear();
            while (Get(GetBlobChunk).Step()) {
                NACCache::THash elem;
                elem.SetUid(ToString(GetColumnText(GetBlobChunk, 0, "Uid")));
                maxRowid = GetColumnInt64(GetBlobChunk, 1, "Rowid");
                out.emplace_back(std::move(elem));
            }
            return maxRowid;
        };
        return Nested_ ? transaction() : TBase::WrapInTransaction(transaction);
    }

    TCASStmts::TCASStmts(TSQLiteDB& db, bool nested) {
        Ref_.Reset(new TImpl(db, nested));
    }

    TCASStmts::~TCASStmts() {
    }

    i64 TCASStmts::GetNextChunk(TListOut& out, i64 startingRowid) {
        return Ref_->GetNextChunk(out, startingRowid);
    }

    TCASStmts::TReturn TCASStmts::PutBlob(const TFsBlobProcessor::TParams& params, TFsBlobProcessor& processor, i32 refCountAdj, TLog& log) {
        return Ref_->PutBlob(params, processor, refCountAdj, log);
    }

    TCASStmts::TReturn TCASStmts::GetBlob(TFsBlobProcessor& processor, TLog& log) {
        return Ref_->GetBlob(processor, log);
    }
}

namespace {
    static const char* AC_QUERIES_NAMES[] = {
        "@AcsRef",
        "@AuxUid",
        "@BlobRef",
        "@EdgeId",
        "@FromUidId",
        "@IsResult",
        "@LastAccess",
        "@LastAccessTime",
        "@NumDeps",
        "@Origin",
        "@RequestCount",
        "@TaskRef",
        "@RefCount",
        "@RelativePath",
        "@ToUidId",
        "@Uid",
        "@Weight"};
}

namespace NACCachePrivate {
    TInsertRunningStmts::TInsertRunningStmts(TSQLiteDB& db)
        : NCachesPrivate::TInsertRunningStmts(db)
    {
    }

    TACStmts::TImpl::TImpl(TSQLiteDB& db)
        // Infinite number of retries
        : TBase(db, size_t(-1), GetACDBMutex())
        , ProcInserter_(db)
    {
        const char* resources[] = {"ac/ac"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &AC_QUERIES_NAMES, db);
        Y_ABORT_UNLESS(Get(GetACOriginAndUse).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GetACRowid).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(GetBlobRefs).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(InsertIntoAC).BoundParameterCount() == 4);
        Y_ABORT_UNLESS(Get(InsertIntoACBlobs).BoundParameterCount() == 3);
        Y_ABORT_UNLESS(Get(InsertIntoACGC).BoundParameterCount() == 4);
        Y_ABORT_UNLESS(Get(InsertIntoReqs).BoundParameterCount() == 2);
        Y_ABORT_UNLESS(Get(UpdateAC).BoundParameterCount() == 5);
        Y_ABORT_UNLESS(Get(UpdateIntoACGC).BoundParameterCount() == 4);
        Y_ABORT_UNLESS(Get(UpdateRequestCount).BoundParameterCount() == 2);
    }

    bool TACStmts::TImpl::SetReturnValue(TOrigin& result, const TString& uidStr) {
        Get(GetACRowid).Bind("@Uid", uidStr);

        if (!Get(GetACRowid).Step()) {
            return false;
        }

        Y_ABORT_UNLESS(Get(GetACRowid).ColumnCount() == 3);
        // Ignore uid's Origin
        auto origin = static_cast<EOrigin>(GetColumnInt64(GetACRowid, 1, "Origin"));
        result.SetOriginKind(origin);
        return true;
    }

    bool TACStmts::TImpl::SetReturnValue(TOrigin& result, i64 acRowid) {
        Get(GetACOriginAndUse).Bind("@AcsRef", acRowid);

        if (!Get(GetACOriginAndUse).Step()) {
            return false;
        }

        Y_ABORT_UNLESS(Get(GetACOriginAndUse).ColumnCount() == 2);
        // Ignore uid's Origin
        auto origin = static_cast<EOrigin>(GetColumnInt64(GetACOriginAndUse, 0, "Origin"));
        result.SetOriginKind(origin);
        return true;
    }

    i64 TACStmts::TImpl::UpdateRequestCountInDBForLock(const NUserService::TPeer& peer, i64 acRowid, i64 refCount) {
        i64 taskRef = 0;
        i64 procId = 0;
        std::tie(taskRef, procId) = ProcInserter_.InsertRunningProc(peer);
        if (!Get(SelectFromReqs).Bind("@AcsRef", acRowid).Bind("@TaskRef", taskRef).Step()) {
            Get(InsertIntoReqs).Bind("@AcsRef", acRowid).Bind("@TaskRef", taskRef).Execute();
            Get(UpdateRequestCount).Bind("@AcsRef", acRowid).Bind("@RequestCount", refCount + 1).Execute();
        }
        return procId;
    }

    i64 TACStmts::TImpl::UpdateRequestCountInDBForUnlock(const NUserService::TPeer& peer, i64 acRowid, i64 refCount) {
        i64 taskRef = 0;
        i64 procId = 0;
        std::tie(taskRef, procId) = ProcInserter_.InsertRunningProc(peer);
        if (Get(SelectFromReqs).Bind("@AcsRef", acRowid).Bind("@TaskRef", taskRef).Step()) {
            Get(DeleteFromReqs).Bind("@AcsRef", acRowid).Bind("@TaskRef", taskRef).Execute();
            Get(UpdateRequestCount).Bind("@AcsRef", acRowid).Bind("@RequestCount", refCount - 1).Execute();
        }
        return procId;
    }

    void TACStmts::TImpl::UpdateGCInformation(i64 acRowid, i64 accessCnt, bool existing, bool resultNode) {
        auto time = MilliSeconds();
        if (existing) {
            Get(UpdateIntoACGC).Bind("@LastAccessTime", (i64)time).Bind("@LastAccess", accessCnt).Bind("@AcsRef", acRowid).Bind("@IsResult", resultNode ? 1 : 0).Execute();
        } else {
            Get(InsertIntoACGC).Bind("@LastAccessTime", (i64)time).Bind("@LastAccess", accessCnt).Bind("@AcsRef", acRowid).Bind("@IsResult", resultNode ? 1 : 0).Execute();
        }
    }

    TACStmts::TReturn TACStmts::TImpl::PutUid(const TPutUid& uidOrig, NACCache::TCASManager& cas, i64 accessCnt) {
        // Capture temp TString to pass as TStringBuf
        const auto& uidStr = uidOrig.GetACHash().GetUid();
        // Keep it for potential call to Bind, which may have stale TStringBuf on return from
        // lambda.
        TString relPath;

        auto casGuard = cas.GetRollbackGuard(uidStr, false);

        // Create a copy for local modifications
        TPutUid uid(uidOrig);
        for (int i = 0, s = uid.BlobInfoSize(); i < s; ++i) {
            auto& blobInfo = *uid.MutableBlobInfo(i);
            cas.PreprocessBlobForPut(blobInfo, casGuard);
        }

        LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACST]") << "PU: uid=" << uidStr << Endl;

        return TBase::WrapInTransaction([this, &uid, &cas, &uidStr, &relPath, &casGuard, accessCnt]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            // Rename is the most aggressive optimization.
            TReturn out({TOrigin(), -1, 0, 0, NACCache::Rename, 0, 0, false});

            auto updateGC = [&uid, &out, this, accessCnt](bool existing, i64 acRowid, i64 refCount) -> void {
                if (uid.HasPeer()) {
                    out.ProcId = UpdateRequestCountInDBForLock(uid.GetPeer(), acRowid, refCount);
                }
                UpdateGCInformation(acRowid, accessCnt, existing, uid.GetResult());
            };

            i64 acRowid = -1;
            i64 refCount = -1;
            bool existing = SetReturnValue(out.Origin, uidStr);
            if (existing) {
                // Process existing uid
                acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");
                refCount = GetColumnInt64(GetACRowid, 2, "RequestCount");
                if (uid.GetReplacementMode() == UseOldBlobs) {
                    // May throw exception
                    out.Success = false;
                    updateGC(existing, acRowid, refCount);
                    casGuard.Commit();
                    return out;
                }
                ssize_t size = 0, fsSize = 0;
                i32 blobDiff = 0;
                std::tie(size, fsSize, blobDiff) = RemoveBlobs(acRowid, cas, casGuard);
                out.TotalSizeDiff += size;
                out.TotalFSSizeDiff += fsSize;
                out.ACsDiff -= 1;
                out.BlobDiff += blobDiff;
            }

            const auto& eorigin = uid.GetOrigin();
            if (existing) {
                Get(UpdateAC)
                    .Bind("@Uid", uidStr)
                    .Bind("@AuxUid", uidStr)
                    .Bind("@Weight", uid.GetWeight())
                    .Bind("@Origin", static_cast<i64>(eorigin.GetOriginKind()))
                    .Bind("@AcsRef", acRowid)
                    .Execute();

                out.Origin.SetOriginKind(eorigin.GetOriginKind());
            } else {
                Get(InsertIntoAC)
                    .Bind("@Uid", uidStr)
                    .Bind("@AuxUid", uidStr)
                    .Bind("@Weight", uid.GetWeight())
                    .Bind("@Origin", static_cast<i64>(eorigin.GetOriginKind()))
                    .Execute();

                SetReturnValue(out.Origin, uidStr);
                acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");
                refCount = GetColumnInt64(GetACRowid, 2, "RequestCount");
            }

            // Done with acs
            TVector<i64> blobRowids;
            if (uid.DBFileNamesSize() != 0) {
                Y_ENSURE_EX(uid.DBFileNamesSize() == uid.BlobInfoSize(), TWithBackTrace<yexception>());
            }
            // Done with tasks
            for (int i = 0, s = uid.BlobInfoSize(); i < s; ++i) {
                const auto& blobInfo = uid.GetBlobInfo(i);
                // Increment refCount.
                auto blobOut = cas.PutBlob(blobInfo, 1, &casGuard);
                if (uid.DBFileNamesSize() != 0) {
                    relPath = uid.GetDBFileNames(i);
                } else {
                    relPath = cas.GetRelativePath(uid.GetRootPath(), blobInfo.GetPath());
                }
                Get(InsertIntoACBlobs)
                    .Bind("@AcsRef", acRowid)
                    .Bind("@BlobRef", blobOut.Rowid)
                    .Bind("@RelativePath", relPath)
                    .Execute();
                out.TotalSizeDiff += blobOut.TotalSizeDiff;
                out.TotalFSSizeDiff += blobOut.TotalFSSizeDiff;
                out.CopyMode = Meet(out.CopyMode, blobOut.CopyMode);
                out.BlobDiff += blobOut.Exists.first != blobOut.Exists.second ? (blobOut.Exists.second ? 1 : -1) : 0;
                // TODO: Deal with error, should not happen
                if (!blobOut.Exists.second) {
                    Y_ASSERT(0);
                    out.Success = false;
                    // casGuard.Rollback();
                    return out;
                }
            }

            // May throw exception
            out.Success = true;
            out.ACsDiff += 1;
            updateGC(existing, acRowid, refCount);
            casGuard.Commit();
            return out;
        });
    }

    TACStmts::TReturn TACStmts::TImpl::GetUid(const TGetUid& uid, NACCache::TCASManager& cas, i64 accessCnt) {
        // Capture temp TString to pass as TStringBuf
        const auto& uidStr = uid.GetACHash().GetUid();

        auto casGuard = cas.GetRollbackGuard(uidStr, false);

        TVector<TBlobInfo> files;

        TReturn output = TBase::WrapInTransaction([this, &uid, &cas, &uidStr, &casGuard, accessCnt, &files]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            TOrigin result;
            if (!SetReturnValue(result, uidStr)) {
                // No data available.
                return TReturn({result, -1, 0, 0, NACCache::Copy, 0, 0, false});
            }
            /// TODO: Process uid.GetFilter().
            auto acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");
            auto refCount = GetColumnInt64(GetACRowid, 2, "RequestCount");
            Get(GetBlobRefs).Bind("@AcsRef", acRowid);

            // Rename is the most aggressive optimization.
            EOptim copyMode = Rename;
            while (Get(GetBlobRefs).Step()) {
                Y_ABORT_UNLESS(Get(GetBlobRefs).ColumnCount() == 2);

                TBlobInfo bi;
                bi.MutableCASHash()->SetUid(TString(GetColumnText(GetBlobRefs, 0, "BlobUid")));
                bi.SetPath(JoinFsPaths(uid.GetDestPath(), TString(GetColumnText(GetBlobRefs, 1, "RelativePath"))));
                bi.SetOptimization(uid.GetOptimization());
                auto status = cas.GetBlob(bi, &casGuard);
                copyMode = Meet(copyMode, status.CopyMode);

                files.emplace_back(std::move(bi));
                // TODO: Deal with error, should not happen
                if (!status.Exists.second) {
                    Y_ASSERT(0);
                    return TReturn({result, -1, 0, 0, NACCache::Copy, 0, 0, false});
                }
            }
            UpdateGCInformation(acRowid, accessCnt, true /*existing*/, uid.GetResult());
            if (uid.GetRelease() && uid.HasPeer()) {
                UpdateRequestCountInDBForUnlock(uid.GetPeer(), acRowid, refCount);
            }
            return TReturn({result, -1, 0, 0, copyMode, 0, 0, true});
        });

        for (auto& bi : files) {
            auto optim = cas.PostprocessBlobAfterGet(bi, casGuard);
            output.CopyMode = Meet(optim, output.CopyMode);
        }

        LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACST]") << "GU: uid=" << uidStr << Endl;

        // May throw exception
        casGuard.Commit();
        return output;
    }

    std::tuple<ssize_t, ssize_t, i32> TACStmts::TImpl::RemoveBlobs(i64 acRowid, NACCache::TCASManager& cas, TRollbackHandler& casGuard) {
        ssize_t size = 0, fsSize = 0;
        i32 blobDiff = 0;
        Get(GetBlobRefs).Bind("@AcsRef", acRowid);
        TVector<TString> bUids;
        while (Get(GetBlobRefs).Step()) {
            Y_ABORT_UNLESS(Get(GetBlobRefs).ColumnCount() == 2);
            bUids.emplace_back(GetColumnText(GetBlobRefs, 0, "BlobUid"));
        }

        Get(DeleteAcsBlob).Bind("@AcsRef", acRowid).Execute();

        for (auto& bUid : bUids) {
            TBlobInfo bi;
            bi.MutableCASHash()->SetUid(bUid);
            bi.SetOptimization(Hardlink);
            // Works as removal.
            auto blobOut = cas.PutBlob(bi, -1, &casGuard);
            size += blobOut.TotalSizeDiff;
            fsSize += blobOut.TotalFSSizeDiff;
            blobDiff += blobOut.Exists.first != blobOut.Exists.second ? (blobOut.Exists.second ? 1 : -1) : 0;
        }
        return std::make_tuple(size, fsSize, blobDiff);
    }

    void TACStmts::TImpl::DeleteAC(i64 acRowid) {
        Get(DeleteDepsFrom).Bind("@AcsRef", acRowid).Execute();
        Get(DeleteDepsTo).Bind("@AcsRef", acRowid).Execute();
        Get(DeleteRequest).Bind("@AcsRef", acRowid).Execute();
        Get(DeleteAcsGc).Bind("@AcsRef", acRowid).Execute();
        Get(RemoveAC).Bind("@AcsRef", acRowid).Execute();
    }

    TACStmts::TReturn TACStmts::TImpl::RemoveUid(const NACCache::TRemoveUid& uid, NACCache::TCASManager& cas) {
        const auto& uidStr = uid.GetACHash().GetUid();
        auto casGuard = cas.GetRollbackGuard(uidStr, true);

        LOGGER_CHECKED_GENERIC_LOG(cas.GetLog(), TRTYLogPreprocessor, TLOG_INFO, "INFO[ACST]") << "RU: uid=" << uidStr << Endl;

        auto out = TBase::WrapInTransaction([this, &cas, &casGuard, &uidStr, &uid]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            TOrigin result;
            if (!SetReturnValue(result, uidStr)) {
                // No data available.
                return TReturn({result, -1, 0, 0, NACCache::Copy, 0, 0, false});
            }

            auto acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");
            auto refCount = GetColumnInt64(GetACRowid, 2, "RequestCount");
            if (uid.GetForcedRemoval() || refCount == 0) {
                return RemoveUidNested(acRowid, cas, casGuard);
            }
            return TReturn({result, -1, 0, 0, NACCache::Copy, 0, 0, false});
        });
        casGuard.Commit();
        return out;
    }

    TACStmts::TReturn TACStmts::TImpl::RemoveUidNested(i64 acRowid, NACCache::TCASManager& cas, TRollbackHandler& casGuard) {
        THolder<TSelf, TStmtResetter<true>> resetter(this);

        ssize_t size = 0, fsSize = 0;
        i32 blobDiff = 0;
        std::tie(size, fsSize, blobDiff) = RemoveBlobs(acRowid, cas, casGuard);
        DeleteAC(acRowid);
        return TReturn({TOrigin(), -1, size, fsSize, NACCache::Copy, -1, blobDiff, true});
    }

    TACStmts::TReturn TACStmts::TImpl::HasUid(const NACCache::THasUid& info, i64 accessCnt) {
        const auto& uidStr = info.GetACHash().GetUid();
        return TBase::WrapInTransaction([this, &info, &uidStr, accessCnt]() -> TReturn {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            TReturn out({TOrigin(), -1, 0, 0, NACCache::Copy, 0, 0, false});

            bool existing = SetReturnValue(out.Origin, uidStr);
            if (existing) {
                // Process existing uid
                i64 acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");
                if (info.HasPeer()) {
                    out.ProcId = UpdateRequestCountInDBForLock(info.GetPeer(), acRowid, GetColumnInt64(GetACRowid, 2, "RequestCount"));
                }
                UpdateGCInformation(acRowid, accessCnt, existing, info.GetResult());
            }
            out.Success = existing;
            return out;
        });
    }

    bool TACStmts::TImpl::PutDeps(const NACCache::TNodeDependencies& deps) {
        auto& uid = deps.GetNodeHash().GetUid();
        TString depUid;
        return TBase::WrapInTransaction([this, &deps, &uid, &depUid]() -> bool {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Get(GetACRowid).Bind("@Uid", uid);
            if (!Get(GetACRowid).Step()) {
                return false;
            }
            auto acRowid = GetColumnInt64(GetACRowid, 0, "Rowid");

            int numDeps = deps.RequiredHashesSize();
            bool result = true;
            for (int i = 0; i < numDeps; ++i) {
                depUid = deps.GetRequiredHashes(i).GetUid();
                Get(GetACRowid).Bind("@Uid", depUid);
                if (!Get(GetACRowid).Step()) {
                    result = false;
                    continue;
                }
                auto depRowid = GetColumnInt64(GetACRowid, 0, "Rowid");

                Get(GCPutEdge).Bind("@FromUidId", acRowid).Bind("@ToUidId", depRowid).Bind("@EdgeId", i).Execute();
            }

            Get(GCSetNumDeps).Bind("@AcsRef", acRowid).Bind("@NumDeps", numDeps).Execute();
            return result;
        });
    }

    TACStmts::TACStmts(TSQLiteDB& db) {
        Ref_.Reset(new TImpl(db));
    }

    TACStmts::~TACStmts() {
    }

    TACStmts::TReturn TACStmts::PutUid(const TPutUid& uid, TCASManager& cas, i64 accessCnt) {
        return Ref_->PutUid(uid, cas, accessCnt);
    }

    TACStmts::TReturn TACStmts::GetUid(const TGetUid& uid, TCASManager& cas, i64 accessCnt) {
        return Ref_->GetUid(uid, cas, accessCnt);
    }

    TACStmts::TReturn TACStmts::RemoveUid(const NACCache::TRemoveUid& uid, NACCache::TCASManager& cas) {
        return Ref_->RemoveUid(uid, cas);
    }

    TACStmts::TReturn TACStmts::RemoveUidNested(i64 acRowid, NACCache::TCASManager& cas, NACCache::TRollbackHandler& casGuard) {
        return Ref_->RemoveUidNested(acRowid, cas, casGuard);
    }

    TACStmts::TReturn TACStmts::HasUid(const NACCache::THasUid& info, i64 accessCnt) {
        return Ref_->HasUid(info, accessCnt);
    }

    bool TACStmts::PutDeps(const NACCache::TNodeDependencies& deps) {
        return Ref_->PutDeps(deps);
    }
}

namespace {
    static const char* STAT_QUERIES_NAMES[] = {
        "@ProcCTime",
        "@ProcPid",
        "@TaskId",
        "@TaskRef"};
}

namespace NACCachePrivate {
    TStatQueriesStmts::TImpl::TImpl(TSQLiteDB& db)
        // Infinite number of retries
        : TBase(db, size_t(-1), GetACDBMutex())
    {
        const char* resources[] = {"ac/stat"};
        CheckAndSetSqlStmts<EStmtName>(resources, Stmts_, &STAT_QUERIES_NAMES, db);
        Y_ABORT_UNLESS(Get(ACsCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(AnalyzeDisk).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(BlobsCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(LastAccessNumber).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(PageCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(PageSize).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(ProcsCount).BoundParameterCount() == 0);
        Y_ABORT_UNLESS(Get(TaskDiskSize).BoundParameterCount() == 1);
        Y_ABORT_UNLESS(Get(TotalDiskSize).BoundParameterCount() == 0);
    }

    TStatus TStatQueriesStmts::TImpl::GetStatistics() {
        TStatus out;

        TBase::WrapInTransactionVoid([this, &out]() -> void {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Y_ABORT_UNLESS(Get(TotalDiskSize).Step());
            out.SetTotalFSSize(GetColumnInt64(TotalDiskSize, 0, "DiskSize"));
            out.SetTotalSize(GetColumnInt64(TotalDiskSize, 1, "Size"));

            if (PageSize_ == 0) {
                Get(PageSize).Step();
                // It is pragma.
                PageSize_ = Get(PageSize).ColumnInt64(0);
            }
            Y_ABORT_UNLESS(Get(PageCount).Step());
            i64 pages = Get(PageCount).ColumnInt64(0);
            out.SetTotalDBSize(PageSize_ * pages);

            Y_ABORT_UNLESS(Get(BlobsCount).Step());
            out.SetBlobCount(GetColumnInt64(BlobsCount, 0, "Cnt"));
            Y_ABORT_UNLESS(Get(ACsCount).Step());
            out.SetUidCount(GetColumnInt64(ACsCount, 0, "Cnt"));
            Y_ABORT_UNLESS(Get(ProcsCount).Step());
            out.SetProcessesCount(GetColumnInt64(ProcsCount, 0, "Cnt"));
        });
        return out;
    }

    int TStatQueriesStmts::TImpl::GetLastAccessNumber() {
        return TBase::WrapInTransaction([this]() -> int {
            THolder<TSelf, TStmtResetter<true>> resetter(this);

            Y_ABORT_UNLESS(Get(LastAccessNumber).Step());
            return GetColumnInt64(LastAccessNumber, 0, "Max");
        });
    }

    void TStatQueriesStmts::TImpl::GetTaskStats(i64 taskRef, NACCache::TTaskStatus* out) {
        THolder<TSelf, TStmtResetter<true>> resetter(this);

        Get(TaskDiskSize).Bind("@TaskRef", taskRef);
        if (Get(TaskDiskSize).Step()) {
            Y_ABORT_UNLESS(Get(TaskDiskSize).ColumnCount() == 2);
            out->SetTotalFSSize(GetColumnInt64(TaskDiskSize, 0, "DiskSize"));
            out->SetTotalSize(GetColumnInt64(TaskDiskSize, 1, "Size"));
        }
    }

    void TStatQueriesStmts::TImpl::GetTaskStats(const NUserService::TPeer& peer, NACCache::TTaskStatus* out) {
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

    void TStatQueriesStmts::TImpl::AnalyzeDU(NACCache::TDiskUsageSummary* out) {
        TBase::WrapInTransactionVoid([this, out]() {
            THolder<TSelf, TStmtResetter<true>> resetter(this);
            out->ClearFileStats();
            while (Get(AnalyzeDisk).Step()) {
                auto* stat = out->AddFileStats();
                stat->SetPath(TString(GetColumnText(AnalyzeDisk, 0, "RelativePath")));
                stat->SetFSSize(GetColumnInt64(AnalyzeDisk, 1, "FileSize"));
                stat->SetSize(GetColumnInt64(AnalyzeDisk, 2, "Size"));
                stat->SetFreq(GetColumnInt64(AnalyzeDisk, 3, "AcsCount"));
            }
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

    void TStatQueriesStmts::GetTaskStats(const NUserService::TPeer& peer, NACCache::TTaskStatus* out) {
        Ref_->GetTaskStats(peer, out);
    }

    void TStatQueriesStmts::AnalyzeDU(NACCache::TDiskUsageSummary* out) {
        Ref_->AnalyzeDU(out);
    }
}

// Instantiate required template
template class NCachesPrivate::TRunningQueriesStmts<NACCachePrivate::TAcsUpdaterOnTaskRemoval, bool, NACCachePrivate::ProcCleanupMode>;

namespace NACCachePrivate {
    TRunningQueriesStmts::TRunningQueriesStmts(TSQLiteDB& db)
        : ::NCachesPrivate::TRunningQueriesStmts<TAcsUpdaterOnTaskRemoval, bool, ProcCleanupMode>(db, GetACDBMutex())
    {
    }
}
