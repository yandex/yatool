#include "fs_blobs.h"
#include <devtools/libs/acdigest/acdigest.h>
#include "devtools/local_cache/common/fs-utils/stats.h"
#include "devtools/local_cache/ac/errors.h"

#include <library/cpp/digest/md5/md5.h>

#include <contrib/libs/zstd/include/zstd.h>

#include <util/charset/utf8.h>
#include <util/generic/scope.h>
#include <util/generic/utility.h>
#include <util/memory/tempbuf.h>
#include <util/stream/file.h>
#include <util/stream/format.h>
#include <util/system/error.h>
#include <util/system/fstat.h>
#include <util/system/tempfile.h>

#include <algorithm>
#include <array>
#include <vector>
#include <memory>

#include <errno.h>

namespace {
    constexpr TStringBuf ZstdSuffix = ".zst";
    constexpr TStringBuf OrphanSuffix = ".orphan";
    constexpr TStringBuf PromoteSuffix = ".promote";
    // A frame is kept only if it saves both of these, otherwise the blob stays plain.
    constexpr i64 MinFrameSavingPercent = 5;
    constexpr i64 MinFrameSavingBytes = 64 << 10;
    constexpr size_t MaxPinnedFrames = 64;

    size_t CheckZstd(size_t code, TStringBuf op, const TString& fileName) {
        if (ZSTD_isError(code)) {
            ythrow TIoException() << "zstd " << op << " failed for " << fileName << ": " << ZSTD_getErrorName(code);
        }
        return code;
    }

    struct TZstdFree {
        void operator()(ZSTD_CCtx* ctx) const noexcept {
            ZSTD_freeCCtx(ctx);
        }
        void operator()(ZSTD_DCtx* ctx) const noexcept {
            ZSTD_freeDCtx(ctx);
        }
    };

    // Single frame, level 1 with content checksum. Pledged size makes zstd fail if the source changes.
    void CompressFile(const TString& from, const TString& to, i64 size) {
        TFile in(from, OpenExisting | RdOnly | Seq);
        TFile out(to, CreateAlways | WrOnly | AWUser | ARUser);
        std::unique_ptr<ZSTD_CCtx, TZstdFree> ctx(ZSTD_createCCtx());
        if (!ctx) {
            ythrow TIoException() << "zstd context allocation failed for " << from;
        }
        CheckZstd(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, 1), "setup", from);
        CheckZstd(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_checksumFlag, 1), "setup", from);
        CheckZstd(ZSTD_CCtx_setPledgedSrcSize(ctx.get(), size), "setup", from);

        TTempBuf inBuf(ZSTD_CStreamInSize());
        TTempBuf outBuf(ZSTD_CStreamOutSize());
        for (;;) {
            size_t read = in.Read(inBuf.Data(), inBuf.Size());
            auto directive = read == 0 ? ZSTD_e_end : ZSTD_e_continue;
            ZSTD_inBuffer input = {inBuf.Data(), read, 0};
            size_t remaining = 0;
            do {
                ZSTD_outBuffer output = {outBuf.Data(), outBuf.Size(), 0};
                remaining = CheckZstd(ZSTD_compressStream2(ctx.get(), &output, &input, directive), "compress", from);
                out.Write(outBuf.Data(), output.pos);
            } while (directive == ZSTD_e_end ? remaining != 0 : input.pos != input.size);
            if (read == 0) {
                break;
            }
        }
    }

    // Positional reads: a frame pinned for several outputs is decoded from the same descriptor.
    void DecompressFile(const TFile& in, const TString& to, i64 size) {
        const auto& from = in.GetName();
        TFile out(to, OpenExisting | WrOnly | Seq);
        std::unique_ptr<ZSTD_DCtx, TZstdFree> ctx(ZSTD_createDCtx());
        if (!ctx) {
            ythrow TIoException() << "zstd context allocation failed for " << from;
        }

        TTempBuf inBuf(ZSTD_DStreamInSize());
        TTempBuf outBuf(ZSTD_DStreamOutSize());
        i64 total = 0;
        i64 offset = 0;
        size_t hint = 1;
        bool pending = false;
        for (;;) {
            size_t read = in.Pread(inBuf.Data(), inBuf.Size(), offset);
            offset += read;
            ZSTD_inBuffer input = {inBuf.Data(), read, 0};
            // Full output buffer may leave decoded data inside the context.
            while (input.pos != input.size || pending) {
                ZSTD_outBuffer output = {outBuf.Data(), outBuf.Size(), 0};
                hint = ZSTD_decompressStream(ctx.get(), &output, &input);
                if (ZSTD_isError(hint)) {
                    ythrow NACCache::TCorruptedBlobError() << "zstd decompress failed for " << from << ": " << ZSTD_getErrorName(hint);
                }
                total += output.pos;
                if (total > size || (hint == 0 && input.pos != input.size)) {
                    ythrow NACCache::TCorruptedBlobError() << "Unexpected data in " << from;
                }
                out.Write(outBuf.Data(), output.pos);
                pending = hint != 0 && output.pos == output.size;
            }
            if (read == 0) {
                break;
            }
        }
        if (hint != 0 || total != size) {
            ythrow NACCache::TCorruptedBlobError() << "Truncated or size mismatch in " << from << ": " << total << " != " << size;
        }
    }

    bool ExistsNoFollow(const TString& path) {
        stat_struct buf;
        try {
            NFsPrivate::LStat(path, &buf);
        } catch (const TIoException& e) {
            return e.Status() != ENOENT;
        }
        return true;
    }
}

namespace NACCache {
    namespace {
        bool SharedMode = false;
    }

    void SetSharedMode(bool enabled) {
        SharedMode = enabled;
    }

    bool IsSharedMode() {
        return SharedMode;
    }

    namespace {
        const NFs::EFilePermissions SharedDirectoryPermissions = NFs::FP_COMMON_FILE;

        i64 SharedOutputMode(i64 mode) {
            const auto permissions = mode & 0777;
            return permissions | ((permissions & 0700) >> 3) | ((permissions & 0700) >> 6);
        }

        bool MakeOutputDirectories(const TFsPath& path) {
            if (!IsSharedMode()) {
                return NFs::MakeDirectoryRecursive(path, StoragePermissions);
            }

            std::vector<TFsPath> missing;
            TFsPath current(path);
            while (!current.Exists()) {
                missing.push_back(current);
                auto parent = current.Dirname();
                if (parent == current) {
                    return false;
                }
                current = parent;
            }

            std::reverse(missing.begin(), missing.end());
            for (const auto& dir : missing) {
                if (!NFs::MakeDirectory(dir, SharedDirectoryPermissions) && !dir.IsDirectory()) {
                    return false;
                }
                NFsPrivate::Chmod(dir, 0777);
            }
            return true;
        }
    }

    TStringBuf CodecName(ECodec codec) {
        switch (codec) {
            case CodecNone:
                return "";
            case CodecZstd:
                return "zstd";
            case CodecUnknown:
                break;
        }
        Y_ABORT("Unknown codec cannot be stored");
    }

    ECodec CodecFromName(TStringBuf name) {
        if (name.empty()) {
            return CodecNone;
        }
        return name == "zstd" ? CodecZstd : CodecUnknown;
    }

    TFsBlobProcessor::TFsBlobProcessor(const TBlobInfo* blobInfo, const TString* rootDir, EOperationMode mode, TCodecState* codec)
        : BlobInfo_(*blobInfo)
        , RootDir_(*rootDir)
        , TransactionLog_(nullptr)
        , Mode_(mode)
        , Codec_(codec)
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

    TString TFsBlobProcessor::GetStoreFileName(ECodec codec) {
        return GetStoreFileName(RootDir_, GetUid(), codec);
    }

    TString TFsBlobProcessor::GetStoreFileName(const TString& storeRoot, const TString& baseBlobName, ECodec codec) {
        auto path = JoinFsPaths(storeRoot, TStringBuf(baseBlobName).SubStr(0, 1), TStringBuf(baseBlobName).SubStr(1, 1), baseBlobName);
        return codec == CodecNone ? path : path + ZstdSuffix;
    }

    void TFsBlobProcessor::PreprocessPut(const TParams& params) {
        Y_ABORT_UNLESS(TransactionLog_);

        if (GetIOMode(params) == NoIOOps) {
            return;
        }

        if (!IsProcessingSplit()) {
            // Encoding under DB lock is not allowed.
            if (Codec_ && params.Codec != CodecNone) {
                ++Codec_->SyncFallbacks;
            }
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
        if (params.Codec == CodecZstd && GetIOMode(params) == FullIO && TryCompressForPut(info)) {
            optim = Copy;
        } else {
            std::tie(content, optim) = PutInternal(params, info);
        }
        TransactionLog_->AddPreprocessPutOrGetResult(GetUid(), content, optim, info);
    }

    bool TFsBlobProcessor::TryCompressForPut(TFsInfo& info) {
        TFileStat stat;
        stat.Mode = info.Mode;
        if (!Codec_ || !stat.IsFile() || info.Size < Codec_->Config.MinSize) {
            return false;
        }
        // Existing file (live or orphan) is reused via the regular path.
        if (ExistsNoFollow(GetStoreFileName(CodecNone)) || ExistsNoFollow(GetStoreFileName(CodecZstd))) {
            ++Codec_->FoundInStore;
            return false;
        }
        if (!Codec_->Encode.try_acquire()) {
            ++Codec_->EncodeBusy;
            return false;
        }
        Y_DEFER {
            Codec_->Encode.release();
        };

        auto frameInfo = CompressToFrame(BlobInfo_.GetPath(), TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile), info.Size);
        if (!frameInfo) {
            ++Codec_->PoorRatio;
            return false;
        }
        info.FSSize = frameInfo->FSSize;
        info.Codec = CodecZstd;
        ++Codec_->Compressed;
        return true;
    }

    TMaybe<TFsBlobProcessor::TFsInfo> TFsBlobProcessor::CompressToFrame(const TString& from, const TString& frame, i64 size) {
#if !defined(_win_)
        // Sparse or compressed by the file system: StatSize counts it full, but a restore would be dense on disk.
        stat_struct source;
        NFsPrivate::LStat(from, &source);
        if (source.st_blocks * 512 < source.st_size) {
            return Nothing();
        }
#endif
        bool keep = false;
        Y_DEFER {
            if (!keep) {
                NFs::Remove(frame);
            }
        };
        CompressFile(from, frame, size);

        auto frameInfo = GetStatInfo(frame);
        if (frameInfo.Size * 100 > size * (100 - MinFrameSavingPercent) || size - frameInfo.Size < MinFrameSavingBytes) {
            return Nothing();
        }
        keep = true;
        return frameInfo;
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
        auto optim = IsSharedMode() ? Copy : BlobInfo_.GetOptimization();
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

        CopyBlob(source, target, info.Mode, false);
        noRemove = true;
        return std::make_tuple("", optim);
    }

    EOptim TFsBlobProcessor::Get(const TParams& params, TStringBuf content, i64 outStoreMode, i64 size) {
        if (GetIOMode(params) == NoIOOps) {
            return BlobInfo_.GetOptimization();
        }

        if (TransactionLog_) {
            TransactionLog_->AddGetFile(BlobInfo_.GetPath());
        }

        if (params.Codec != CodecNone && GetIOMode(params) == FullIO) {
            if (params.Codec != CodecZstd) {
                ythrow TIoException() << "Unknown codec for blob " << GetUid();
            }
            TFsInfo dbInfo = {size, 0, outStoreMode, params.Codec};
            if (!TransactionLog_) {
                MakeTargetDir();
                DecompressBlob(TFile(GetStoreFileName(params.Codec), OpenExisting | RdOnly | Seq), BlobInfo_.GetPath(), dbInfo);
                return Copy;
            }
            if (IsProcessingSplit()) {
                // Pin the frame, decompression is done in PostprocessGet.
                TransactionLog_->AddPreparedPutFile(GetUid());
                TransactionLog_->CreateStashDirs();
                if (!TransactionLog_->HasPreprocesedPutOrGetResult(GetUid())) {
                    (void)GetInternal(TParams({CodecNone, OnFS}), Hardlink, GetStoreFileName(params.Codec), TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile), "", outStoreMode);
                }
            } else if (!TransactionLog_->GetPinnedFrame(GetUid())) {
                TFile frame(GetStoreFileName(params.Codec), OpenExisting | RdOnly | Seq);
                if (TransactionLog_->PinnedFrames_.size() >= MaxPinnedFrames) {
                    // A descriptor per output would exhaust the fd limit on large nodes: decode the rest under DB lock.
                    MakeTargetDir();
                    DecompressBlobUnderLock(frame, BlobInfo_.GetPath(), dbInfo);
                    TransactionLog_->AddGetResult(GetUid(), Copy);
                    return Copy;
                }
                // No stash in sync mode: the open frame outlives promote or GC until PostprocessGet.
                TransactionLog_->PinFrame(GetUid(), std::move(frame));
            }
            TransactionLog_->AddPreprocessPutOrGetResult(GetUid(), "", Copy, dbInfo);
            return Copy;
        }

        auto source = GetIOMode(params) == FullIO ? GetStoreFileName() : TString();
        if (IsProcessingSplit()) {
            TransactionLog_->AddPreparedPutFile(GetUid());
            TransactionLog_->CreateStashDirs();
            if (!TransactionLog_->HasPreprocesedPutOrGetResult(GetUid())) {
                (void)GetInternal(params, IsSharedMode() ? Copy : Hardlink, source, TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile), content, outStoreMode);
            }
            auto optim = IsSharedMode() ? Copy : Hardlink;
            TransactionLog_->AddPreprocessPutOrGetResult(GetUid(), "", optim, TFsInfo());
            return optim;
        }

        MakeTargetDir();

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

        if (TransactionLog_->HasPreprocesedPutOrGetResult(GetUid())) {
            TFsInfo dbInfo;
            (void)TransactionLog_->PreprocessPutOrGetResult(GetUid(), dbInfo);
            if (dbInfo.Codec != CodecNone) {
                MakeTargetDir();
                if (IsProcessingSplit()) {
                    DecompressBlob(TFile(TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile), OpenExisting | RdOnly | Seq), BlobInfo_.GetPath(), dbInfo);
                } else {
                    DecompressBlob(*TransactionLog_->GetPinnedFrame(GetUid()), BlobInfo_.GetPath(), dbInfo);
                }
                // Only hardlinked target may share inode with the store; shared store never links user outputs.
                if (Codec_ && Codec_->Config.PromoteOnGet && BlobInfo_.GetOptimization() == Hardlink && !IsSharedMode()) {
                    // Sync fallback shares its stash with other sync requests of the uid and cleans it outside DB lock.
                    if (IsProcessingSplit()) {
                        try {
                            TransactionLog_->CreateStashDirs();
                            auto promote = TransactionLog_->GetStashedName(GetUid() + PromoteSuffix, TTransactionLog::NewFile);
                            if (NFs::HardLink(BlobInfo_.GetPath(), promote)) {
                                PromoteFile_ = promote;
                            }
                        } catch (const yexception&) {
                        }
                    }
                    Codec_->PromoteSkipped += PromoteFile_.empty();
                }
                return Copy;
            }
        }

        if (IsProcessingSplit()) {
            MakeTargetDir();

            auto sourcePath = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile);
            return GetInternal(TParams({params.Codec, OnFS}), BlobInfo_.GetOptimization(), sourcePath, BlobInfo_.GetPath(), "", GetInfo(InStore).Mode);
        }

        return TransactionLog_->GetResult(GetUid());
    }

    EOptim TFsBlobProcessor::GetInternal(const TParams& params, EOptim optim, const TString& sourceFile, const TString& targetFile, TStringBuf content, i64 outStoreMode) {
        Y_ABORT_UNLESS(params.Mode != DataRemoved);
        Y_ABORT_UNLESS(optim != Rename);

        if (IsSharedMode()) {
            optim = Copy;
        }

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
                NFsPrivate::Chmod(target, IsSharedMode() ? SharedOutputMode(outStoreMode) : outStoreMode);
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

        CopyBlob(source, target, outStoreMode, IsSharedMode());
        noRemove = true;
        return optim;
    }

    void TFsBlobProcessor::MakeTargetDir() const {
        if (!MakeOutputDirectories(TFsPath(BlobInfo_.GetPath()).Dirname())) {
            ythrow TIoException() << "failed to create target dir for " << BlobInfo_.GetPath();
        }
    }

    void TFsBlobProcessor::DecompressBlob(const TFile& source, const TString& target, const TFsInfo& dbInfo) const {
        if (Codec_) {
            Codec_->Decode.acquire();
        }
        Y_DEFER {
            if (Codec_) {
                Codec_->Decode.release();
            }
        };
        DecompressBlobUnderLock(source, target, dbInfo);
    }

    void TFsBlobProcessor::DecompressBlobUnderLock(const TFile& source, const TString& target, const TFsInfo& dbInfo) const {
        if (dbInfo.Codec != CodecZstd) {
            ythrow TIoException() << "Unknown codec for blob " << source.GetName();
        }
        auto tmp = MakeTempName(TFsPath(target).Parent().c_str(), ".ac-", "tmp");
        bool renamed = false;
        Y_DEFER {
            if (!renamed) {
                NFs::Remove(tmp);
            }
        };
        DecompressFile(source, tmp, dbInfo.Size);
        NFsPrivate::Chmod(tmp, IsSharedMode() ? SharedOutputMode(dbInfo.Mode) : dbInfo.Mode & 0777);
        if (!NFs::Rename(tmp, target)) {
            ythrow TIoException() << "Cannot rename " << tmp << " to " << target;
        }
        renamed = true;
        if (Codec_) {
            ++Codec_->Decoded;
        }
    }

    void TFsBlobProcessor::RemoveBlob(const TParams& params, TFsInfo& info) {
        if (GetIOMode(params) != FullIO) {
            if (Mode_ == NoIO) {
                info = GetInfo(InStore);
            }
            return;
        }
        if (params.Codec == CodecUnknown) {
            // Cannot name the file, leave it to a server that knows the codec.
            return;
        }
        auto fileName = GetStoreFileName(params.Codec);
        info = params.Codec == CodecNone ? GetInfo(InStore) : GetStatInfo(fileName);
        if (!TransactionLog_) {
            if (!NFs::Remove(fileName)) {
                ythrow TIoException() << "Cannot remove blob: " << fileName;
            }
        } else {
            auto stashedName = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::OldFile);
            TransactionLog_->AddRemoveFile(GetUid(), params.Codec);
            TransactionLog_->CreateStashDirs();
            if (!NFs::Rename(fileName, stashedName)) {
                ythrow TIoException() << "Cannot stash blob " << fileName << " to " << TransactionLog_->StashDir_;
            }
        }
    }

    bool TFsBlobProcessor::IsMissingInStore(const TParams& params) {
        if (GetIOMode(params) != FullIO) {
            return false;
        }
        if (TransactionLog_ && TransactionLog_->PutFiles_.contains(GetUid())) {
            return false;
        }
        return TFileStat(GetStoreFileName(CodecNone), true /* nofollow */).IsNull() &&
               TFileStat(GetStoreFileName(CodecZstd), true /* nofollow */).IsNull();
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
            i64 size = GetUid().size();
            return TFsInfo({size, size, 0777});
        }
        if (placement == InStore) {
            if (TransactionLog_) {
                auto savedName = TransactionLog_->GetStashedName(GetUid(), TTransactionLog::NewFile);
                const TFsPath savedPath(savedName);
                if (savedPath.Exists() || savedPath.IsSymlink()) {
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

    void TFsBlobProcessor::CopyBlob(const TFsPath& from, const TFsPath& to, i64 fileMode, bool sharedOutput) const {
        if (NFsPrivate::IsLink(fileMode)) {
            if (!NFs::SymLink(from.ReadLink(), to)) {
                ythrow TIoException() << "Cannot create symlink";
            }
            return;
        }

        NFs::Copy(from, to);
        NFsPrivate::Chmod(to, sharedOutput ? SharedOutputMode(fileMode) : fileMode & 0777);

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

    ECodec TFsBlobProcessor::TTransactionLog::GetStashedCodec(const TString& fileHash, EFileState state) const {
        if (state == OldFile) {
            auto it = RemovedCodecs_.find(fileHash);
            return it == RemovedCodecs_.end() ? CodecNone : it->second;
        }
        auto it = PreprocessPutOrGetResults_.find(fileHash);
        return it == PreprocessPutOrGetResults_.end() ? CodecNone : std::get<2>(it->second).Codec;
    }

    void TFsBlobProcessor::TTransactionLog::RenameBlobs(const TSet<TString>& rename, EFileState state) {
        for (auto& fileHash : rename) {
            auto stashedName = GetStashedName(fileHash, state);
            if (!NFs::Rename(stashedName, TFsBlobProcessor::GetStoreFileName(StoreRoot_, fileHash, GetStashedCodec(fileHash, state)))) {
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
        RemovedCodecs_.clear();
        NewGetFiles_.clear();
        PreprocessPutOrGetResults_.clear();
        GetResults_.clear();
    }

    void TFsBlobProcessor::TTransactionLog::Rollback(EOperationMode mode) {
        Y_DEFER {
            PutFiles_.clear();
            PreparedPutFiles_.clear();
            RemoveFiles_.clear();
            RemovedCodecs_.clear();
            NewGetFiles_.clear();
            PreprocessPutOrGetResults_.clear();
            GetResults_.clear();
        };

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
    }

    TFsBlobProcessor::TOrphanStats TFsBlobProcessor::RemoveOrphanFrames(const TString& storeRoot, const THashSet<TString>& known,
                                                                        const std::function<bool(const TString& uid, const std::function<void()>& action)>& unlessRow, TDuration minAge,
                                                                        const std::function<bool(const TString&)>& removePlain) {
        TOrphanStats stats;
        const auto now = TInstant::Now();
        const char digits[] = "0123456789abcdef";
        // ponytail: lists every store directory in one pass, add cancellation if startup gets slow.
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                TFsPath dir = TFsPath(storeRoot) / TString(1, digits[i]) / TString(1, digits[j]);
                TVector<TString> names;
                try {
                    dir.ListNames(names);
                } catch (const TIoException&) {
                    continue;
                }
                for (const auto& name : names) {
                    TStringBuf uid(name);
                    if (removePlain && known.contains(uid)) {
                        // Plain copy left by background compression.
                        TFileStat stat((dir / name).GetPath(), true);
                        if (stat.IsFile() && stat.NLinks == 1 && TInstant::Seconds(stat.MTime) + minAge <= now && removePlain(name)) {
                            ++stats.PlainRemoved;
                        }
                        continue;
                    }
                    bool leftover = uid.ChopSuffix(OrphanSuffix);
                    if (!uid.ChopSuffix(ZstdSuffix) || (!leftover && known.contains(uid))) {
                        continue;
                    }
                    auto frame = (dir / (TString(uid) + ZstdSuffix)).GetPath();
                    auto aside = frame + OrphanSuffix;
                    if (!leftover) {
                        TFileStat stat(frame, true);
                        if (TInstant::Seconds(stat.MTime) + minAge > now) {
                            ++stats.Fresh;
                            continue;
                        }
                    }
                    // A frame committed by the cold pass after the snapshot already has its row under the lock.
                    bool moved = false;
                    bool hasRow = !unlessRow(TString(uid), [&]() {
                        moved = leftover || NFs::Rename(frame, aside);
                    });
                    if (!hasRow) {
                        if (moved && NFs::Remove(aside)) {
                            ++stats.Removed;
                        }
                    } else if (!leftover) {
                        ++stats.Kept;
                    } else if (!ExistsNoFollow(frame)) {
                        // Moved aside by an interrupted run of an older version.
                        NFs::Rename(aside, frame);
                        ++stats.Kept;
                    } else if (NFs::Remove(aside)) {
                        ++stats.Removed;
                    }
                }
            }
        }
        return stats;
    }
}
