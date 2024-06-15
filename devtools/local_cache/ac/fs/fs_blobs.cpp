#include "fs_blobs.h"
#include <devtools/libs/acdigest/acdigest.h>
#include "devtools/local_cache/common/fs-utils/stats.h"

#include <library/cpp/digest/md5/md5.h>

#include <util/charset/utf8.h>
#include <util/generic/scope.h>
#include <util/generic/utility.h>
#include <util/memory/tempbuf.h>
#include <util/stream/file.h>
#include <util/stream/format.h>
#include <util/system/error.h>

#include <array>

#include <errno.h>

namespace NACCache {
    TFsBlobProcessor::TFsBlobProcessor(const TBlobInfo* blobInfo, const TString* rootDir, EOperationMode mode)
        : BlobInfo_(*blobInfo)
        , RootDir_(*rootDir)
        , TransactionLog_(nullptr)
        , Mode_(mode)
    {
    }

    /// placement == InStore is only suitable for hash verification.
    TString TFsBlobProcessor::GetUid(EFilePlacement placement) {
        Y_ASSERT(Mode_ == Regular);

        auto fileName = GetFileName(placement);
        return NACDigest::GetFileDigest(fileName).Uid;
    }

    const TString& TFsBlobProcessor::GetUid() {
        if (!SUid_.Empty()) {
            return SUid_.GetRef();
        }

        if (BlobInfo_.HasCASHash()) {
            SUid_ = BlobInfo_.GetCASHash().GetUid();
            return SUid_.GetRef();
        }

        Y_ASSERT(Mode_ == Regular);
        SUid_ = GetUid(OutStore);
        return SUid_.GetRef();
    }

    TFsBlobProcessor::TFsInfo TFsBlobProcessor::GetStatInfo(const TString& fileName) {
        stat_struct buf;
        NFsPrivate::LStat(fileName, &buf);
        i64 size = buf.st_size;
        i64 fsSize = NFsPrivate::StatSize(&buf);
        return TFsInfo({size, fsSize, buf.st_mode});
    }

    TString TFsBlobProcessor::GetStoreFileName() {
        return GetStoreFileName(RootDir_, GetUid());
    }

    TString TFsBlobProcessor::GetStoreFileName(const TString& storeRoot, const TString& baseBlobName) {
        return JoinFsPaths(storeRoot, TStringBuf(baseBlobName).SubStr(0, 1), TStringBuf(baseBlobName).SubStr(1, 1), baseBlobName);
    }

    void TFsBlobProcessor::PreprocessPut(const TParams& params) {
        Y_ABORT_UNLESS(TransactionLog_);

        if (GetIOMode(params) == NoIOOps) {
            return;
        }

        if (!IsProcessingSplit()) {
            return;
        }

        if (TransactionLog_ && GetIOMode(params) == FullIO) {
            TransactionLog_->AddPreparedPutFile(GetUid());
            TransactionLog_->CreateStashDirs();
        }

        if (TransactionLog_->HasPreprocesedPutOrGetResult(GetUid())) {
            return;
        }

        // Compute hash and copy/hardlink to stash directory.
        TString content;
        TFsInfo info = GetInfo(OutStore);
        EOptim optim = BlobInfo_.GetOptimization();
        std::tie(content, optim) = PutInternal(params, info);
        TransactionLog_->AddPreprocessPutOrGetResult(GetUid(), content, optim, info);
    }

    std::tuple<TString, EOptim> TFsBlobProcessor::Put(const TParams& params, TFsInfo& info) {
        if (GetIOMode(params) == NoIOOps) {
            info = GetInfo(OutStore);
            return std::make_pair("", BlobInfo_.GetOptimization());
        }

        if (TransactionLog_ && GetIOMode(params) == FullIO) {
            TransactionLog_->AddPutFile(GetUid());
            TransactionLog_->CreateStashDirs();
        }

        if (!IsProcessingSplit()) {
            info = GetInfo(OutStore);
            return PutInternal(params, info);
        }

        // Final action will be performed in TransactionLog_'s Commit.
        return TransactionLog_->PreprocessPutOrGetResult(GetUid(), info);
    }

    std::tuple<TString, EOptim> TFsBlobProcessor::PutInternal(const TParams& params, TFsInfo& info) {
        auto optim = BlobInfo_.GetOptimization();
        Y_ABORT_UNLESS(params.Mode != DataRemoved);

        TFsPath source(BlobInfo_.GetPath());

        if (params.Mode == DataInPlace) {
            // No FS info in store, but still need mode, etc.
            if (NFsPrivate::IsLink(info.Mode)) {
                TFsPath filePath(source);
                return std::make_tuple(filePath.ReadLink(), optim);
            } else {
                EOpenMode perm = AWUser | ARUser;
                EOpenMode mode = OpenAlways | RdOnly;
                auto content = TUnbufferedFileInput(TFile(source, mode | perm)).ReadAll();

                CheckFile(content, source);
                return std::make_tuple(content, optim);
            }
        }

        TFsPath target(TransactionLog_ ? TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile) : GetStoreFileName());

        bool noRemove = false;
        Y_DEFER {
            if (!noRemove) {
                NFs::Remove(target);
            } else if (optim == Copy) {
                info = GetInfo(InStore);
            }
        };

        try {
            if (optim == Rename) {
                if (NFs::Rename(source, target)) {
                    noRemove = true;
                    return std::make_tuple("", optim);
                }
                optim = Hardlink;
            }

            if (optim == Hardlink) {
                if (NFs::HardLink(source, target)) {
                    noRemove = true;
                    return std::make_tuple("", optim);
                }
                optim = Copy;
            }
        } catch (const TSystemError&) {
            optim = Copy;
        }

        CopyBlob(source, target, info.Mode);
        noRemove = true;
        return std::make_tuple("", optim);
    }

    EOptim TFsBlobProcessor::Get(const TParams& params, TStringBuf content, i64 outStoreMode) {
        if (GetIOMode(params) == NoIOOps) {
            return BlobInfo_.GetOptimization();
        }

        if (TransactionLog_) {
            TransactionLog_->AddGetFile(BlobInfo_.GetPath());
        }

        auto source = GetIOMode(params) == FullIO ? GetStoreFileName() : TString();
        if (IsProcessingSplit()) {
            TransactionLog_->AddPreparedPutFile(GetUid());
            TransactionLog_->CreateStashDirs();
            if (!TransactionLog_->HasPreprocesedPutOrGetResult(GetUid())) {
                (void)GetInternal(params, Hardlink, source, TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile), content, outStoreMode);
            }
            TransactionLog_->AddPreprocessPutOrGetResult(GetUid(), "", Hardlink, TFsInfo());
            return Hardlink;
        }

        if (!NFs::MakeDirectoryRecursive(TFsPath(BlobInfo_.GetPath()).Dirname(), StoragePermissions)) {
            ythrow TIoException() << "failed to create target dir for " << BlobInfo_.GetPath();
        }

        auto optimOut = GetInternal(params, BlobInfo_.GetOptimization(), source, BlobInfo_.GetPath(), content, outStoreMode);
        if (TransactionLog_) {
            TransactionLog_->AddGetResult(GetUid(), optimOut);
        }
        return optimOut;
    }

    EOptim TFsBlobProcessor::PostprocessGet(const TParams& params) {
        Y_ABORT_UNLESS(TransactionLog_);

        if (GetIOMode(params) == NoIOOps) {
            return BlobInfo_.GetOptimization();
        }

        if (IsProcessingSplit()) {
            if (!NFs::MakeDirectoryRecursive(TFsPath(BlobInfo_.GetPath()).Dirname(), StoragePermissions)) {
                ythrow TIoException() << "failed to create target dir for " << BlobInfo_.GetPath();
            }

            auto sourcePath = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile);
            return GetInternal(TParams({params.Codec, OnFS}), BlobInfo_.GetOptimization(), sourcePath, BlobInfo_.GetPath(), "", GetInfo(InStore).Mode);
        }

        return TransactionLog_->GetResult(GetUid());
    }

    EOptim TFsBlobProcessor::GetInternal(const TParams& params, EOptim optim, const TString& sourceFile, const TString& targetFile, TStringBuf content, i64 outStoreMode) {
        Y_ABORT_UNLESS(params.Mode != DataRemoved);
        Y_ABORT_UNLESS(optim != Rename);

        TFsPath source(sourceFile);
        TFsPath target(targetFile);
        bool noRemove = false;
        Y_DEFER {
            if (!noRemove) {
                NFs::Remove(target);
            }
        };

        // Remove target to avoid permission problems
        NFs::Remove(target);

        if (params.Mode == DataInPlace) {
            if (NFsPrivate::IsLink(outStoreMode)) {
                if (!NFs::SymLink(ToString(content), target)) {
                    ythrow TIoException() << "Cannot create symlink";
                }
            } else {
                EOpenMode perm = AWUser | ARUser;
                EOpenMode mode = OpenAlways | WrOnly;
                TUnbufferedFileOutput(TFile(target, perm | mode)).Write(content);
                NFsPrivate::Chmod(target, outStoreMode);
                CheckFile(content, target);
            }
            noRemove = true;
            return optim;
        }

        try {
            if (optim == Hardlink) {
                if (NFs::HardLink(source, target)) {
                    noRemove = true;
                    return optim;
                }
                optim = Copy;
            }
        } catch (const TSystemError&) {
            optim = Copy;
        }

        CopyBlob(source, target, outStoreMode);
        noRemove = true;
        return optim;
    }

    void TFsBlobProcessor::RemoveBlob(const TParams& params, TFsInfo& info) {
        if (GetIOMode(params) != FullIO) {
            if (Mode_ == NoIO) {
                info = GetInfo(InStore);
            }
            return;
        }
        auto fileName = GetStoreFileName();
        info = GetInfo(InStore);
        if (!TransactionLog_) {
            if (!NFs::Remove(fileName)) {
                ythrow TIoException() << "Cannot remove blob: " << fileName;
            }
        } else {
            auto stashedName = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::OldFile);
            TransactionLog_->AddRemoveFile(GetUid());
            TransactionLog_->CreateStashDirs();
            if (!NFs::Rename(fileName, stashedName)) {
                ythrow TIoException() << "Cannot stash blob " << fileName << " to " << TransactionLog_->StashDir_;
            }
        }
    }

    std::pair<TString, bool> TFsBlobProcessor::PrepareStashDir(const TString& rootDir, const TString& tid, EOperationMode mode, bool sync) {
        auto out = JoinFsPaths(rootDir, "rm", MD5::Calc(tid));

        if (mode == NoIO || sync) {
            return std::make_pair(out, sync);
        }

        sync = true;
        for (int i = 0; i < 10; ++i) {
            auto dir = out + "-" + ToString(i);
            if (NFs::MakeDirectory(dir)) {
                out = dir;
                sync = false;
                break;
            }
        }
        return std::make_pair(out, sync);
    }

    void TFsBlobProcessor::StartStash(TTransactionLog& transactionLog) {
        TransactionLog_ = &transactionLog;
    }

    THolder<TFsBlobProcessor::TTransactionLog> TFsBlobProcessor::GetTransactionLog(const TString* rootDir, const TString& tid, EOperationMode mode, bool sync) {
        TString stashDir;
        std::tie(stashDir, sync) = TFsBlobProcessor::PrepareStashDir(*rootDir, tid, mode, sync);
        return THolder<TFsBlobProcessor::TTransactionLog>(new TFsBlobProcessor::TTransactionLog(sync, stashDir, rootDir, mode));
    }

    TFsBlobProcessor::TFsInfo TFsBlobProcessor::GetInfo(EFilePlacement placement) {
        if (Mode_ == NoIO) {
            i64 size = GetUid().Size();
            return TFsInfo({size, size, 0777});
        }
        if (placement == InStore) {
            if (TransactionLog_) {
                auto savedName = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile);
                if (TFsPath(savedName).Exists()) {
                    return GetStatInfo(savedName);
                }
            }
            return GetStatInfo(GetStoreFileName());
        }
        return GetStatInfo(BlobInfo_.GetPath());
    }

    void TFsBlobProcessor::CheckFile(TStringBuf content, const TFsPath& fileName) const {
        stat_struct buf;
        NFsPrivate::LStat(fileName, &buf);
        if (static_cast<size_t>(buf.st_size) != content.size()) {
            ythrow TIoException() << "Size mismatch for file: " << fileName;
        }

        EOpenMode perm = AWUser | ARUser;
        EOpenMode mode = OpenAlways | RdOnly;
        TFile out(fileName, perm | mode);

        // Check head and tail of file.
        auto checkSize = Min<size_t>(DIGEST_CHECK_SIZE, content.size());
        std::array<size_t, 2> checks = {(size_t)0, content.size() - checkSize};
        for (size_t i = 0, e = content.size() - checkSize == 0 ? 1 : 2; i != e; ++i) {
            auto pos = checks[i];
            out.Seek(pos, sSet);
            TString buf(content.SubStr(pos, checkSize));

            TFileInput outs(out);
            TStringInput ins(buf);

            auto inDigest = NACDigest::GetStreamDigest(ins, checkSize);
            auto outDigest = NACDigest::GetStreamDigest(outs, checkSize);

            if (inDigest != outDigest) {
                ythrow TIoException() << "Digest mismatch for file: " << fileName << ", at position: " << pos;
            }
        }
    }

    void TFsBlobProcessor::CheckFile(const TFsPath& fromFileName, const TFsPath& toFileName, i64 size) const {
        stat_struct buf;
        NFsPrivate::LStat(toFileName, &buf);
        if (buf.st_size != size) {
            ythrow TIoException() << "Size mismatch for files: " << fromFileName << ", " << toFileName;
        }

        EOpenMode perm = AWUser | ARUser;
        EOpenMode mode = OpenAlways | RdOnly;
        TFile in(fromFileName, perm | mode), out(toFileName, perm | mode);

        // Check head and tail of file.
        auto checkSize = Min<size_t>(DIGEST_CHECK_SIZE, size);
        std::array<size_t, 2> checks = {(size_t)0, size - checkSize};
        for (size_t i = 0, e = size - checkSize == 0 ? 1 : 2; i != e; ++i) {
            auto pos = checks[i];
            in.Seek(pos, sSet);
            out.Seek(pos, sSet);

            TFileInput ins(in), outs(out);

            auto inDigest = NACDigest::GetStreamDigest(ins, checkSize);
            auto outDigest = NACDigest::GetStreamDigest(outs, checkSize);

            if (inDigest != outDigest) {
                ythrow TIoException() << "Digest mismatch for files: " << fromFileName << ", " << toFileName << ", at position: " << pos;
            }
        }
    }

    void TFsBlobProcessor::CopyBlob(const TFsPath& from, const TFsPath& to, i64 fileMode) const {
        if (NFsPrivate::IsLink(fileMode)) {
            if (!NFs::SymLink(from.ReadLink(), to)) {
                ythrow TIoException() << "Cannot create symlink";
            }
            return;
        }

        NFs::Copy(from, to);
        NFsPrivate::Chmod(to, fileMode & 0777);

        stat_struct buf;
        NFsPrivate::LStat(from, &buf);
        CheckFile(from, to, buf.st_size);
    }

    TFsBlobProcessor::TTransactionLog::~TTransactionLog() {
        Y_ASSERT(NewGetFiles_.empty());
        Y_ASSERT(PreparedPutFiles_.empty());
        Y_ASSERT(PutFiles_.empty());
        Y_ASSERT(RemoveFiles_.empty());
        Y_ASSERT(PreprocessPutOrGetResults_.empty());
        Y_ASSERT(GetResults_.empty());
    }

    TString TFsBlobProcessor::TTransactionLog::GetStashedName(const TString& fileHash, EFileState state) {
        return JoinFsPaths(GetStashDir(state), fileHash);
    }

    TString TFsBlobProcessor::TTransactionLog::GetStashDir(EFileState state) {
        return JoinFsPaths(StashDir_, state == OldFile ? "old" : "new");
    }

    void TFsBlobProcessor::TTransactionLog::RemoveBlobs(const TSet<TString>& remove, EFileState state) {
        for (auto& fileHash : remove) {
            auto stashedName = GetStashedName(fileHash, state);
            if (!NFs::Remove(stashedName) && LastSystemError() != ENOENT) {
                ythrow TIoException() << "Cannot remove stashed blob: " << fileHash << " from " << StashDir_;
            }
        }
    }

    void TFsBlobProcessor::TTransactionLog::RenameBlobs(const TSet<TString>& rename, EFileState state) {
        for (auto& fileHash : rename) {
            auto stashedName = GetStashedName(fileHash, state);
            if (!NFs::Rename(stashedName, TFsBlobProcessor::GetStoreFileName(StoreRoot_, fileHash))) {
                ythrow TIoException() << "Cannot rename stashed blob: " << fileHash << " from " << StashDir_;
            }
        }
    }

    void TFsBlobProcessor::TTransactionLog::CreateStashDirs() {
        if (DirectoriesPrepared_) {
            return;
        }

        if (Sync_) {
            if (!NFs::Exists(StashDir_) && !NFs::MakeDirectory(StashDir_)) {
                ythrow TIoException() << "Cannot create directory: " << StashDir_;
            }
        }

        auto outNew = TTransactionLog::GetStashDir(NewFile);
        if (!NFs::Exists(outNew) && !NFs::MakeDirectory(outNew)) {
            ythrow TIoException() << "Cannot create directory: " << outNew;
        }

        auto outOld = TTransactionLog::GetStashDir(OldFile);
        if (!NFs::Exists(outOld) && !NFs::MakeDirectory(outOld)) {
            ythrow TIoException() << "Cannot create directory: " << outOld;
        }

        DirectoriesPrepared_ = true;
    }

    void TFsBlobProcessor::TTransactionLog::CleanupStashDirs() {
        if (!DirectoriesPrepared_) {
            return;
        }
        for (auto dir : {GetStashDir(OldFile), GetStashDir(NewFile), StashDir_}) {
            NFs::RemoveRecursive(dir);
        }
    }

    void TFsBlobProcessor::TTransactionLog::Commit(EOperationMode mode) {
        if (mode == NoIO) {
            return;
        }
        // Remove old blobs
        RemoveBlobs(RemoveFiles_, OldFile);
        // Remove redundant new blobs
        RemoveBlobs(PreparedPutFiles_, NewFile);
        // Rename new required blobs
        RenameBlobs(PutFiles_, NewFile);
        CleanupStashDirs();

        PutFiles_.clear();
        PreparedPutFiles_.clear();
        RemoveFiles_.clear();
        NewGetFiles_.clear();
        PreprocessPutOrGetResults_.clear();
        GetResults_.clear();
    }

    void TFsBlobProcessor::TTransactionLog::Rollback(EOperationMode mode) {
        if (mode == NoIO) {
            return;
        }

        // Remove all new files for Get/Put etc.
        RemoveBlobs(PutFiles_, NewFile);
        RemoveBlobs(PreparedPutFiles_, NewFile);
        // Restore removed files
        RenameBlobs(RemoveFiles_, OldFile);
        CleanupStashDirs();

        for (auto& fileName : NewGetFiles_) {
            if (!NFs::Remove(fileName) && LastSystemError() != ENOENT) {
                ythrow TIoException() << "Cannot remove new file: " << fileName;
            }
        }

        PutFiles_.clear();
        PreparedPutFiles_.clear();
        RemoveFiles_.clear();
        NewGetFiles_.clear();
        PreprocessPutOrGetResults_.clear();
        GetResults_.clear();
    }
}
