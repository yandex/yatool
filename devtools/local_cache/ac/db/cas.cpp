#include "cas.h"

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/logger/null.h>
#include <library/cpp/logger/rotating_file.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/hash_set.h>
#include <util/generic/scope.h>
#include <util/system/datetime.h>
#include <util/system/fstat.h>

namespace NACCache {
    using namespace NSQLite;

    bool operator==(const THash& a, const THash& b) {
        return a.GetUid() == b.GetUid();
    }
}

namespace {
    constexpr size_t MaxColdSkips = 100000;
}

namespace NACCache {
    using namespace NSQLite;
    TCASManager::TCASManager(NSQLite::TSQLiteDB& db, const TString& rootDir, bool nested, TFsBlobProcessor::EOperationMode mode, bool enableLogging, const TString& logPrefix, bool recreate, const TCodecConfig& codec)
        : DBProcessing_(db, nested)
        , RootDir_(rootDir)
        , Mode_(mode)
        , Codec_(MakeHolder<TCodecState>(codec))
    {
        if (!NFs::MakeDirectoryRecursive(RootDir_)) {
            ythrow TIoException() << "failed to create " << RootDir_;
        }
        if (!NFs::MakeDirectoryRecursive(JoinFsPaths(RootDir_, "rm"))) {
            ythrow TIoException() << "failed to create " << RootDir_;
        }
        const char* digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f"};
        for (auto i : digits) {
            auto outerDir = CreateNumberedDir(RootDir_, i);
            for (auto j : digits) {
                if (recreate) {
                    NFs::RemoveRecursive(JoinFsPaths(outerDir, j));
                }
                CreateNumberedDir(outerDir, j);
            }
        }
        if (enableLogging && Mode_ != TFsBlobProcessor::NoIO) {
            Log_.ResetBackend(MakeHolder<TRotatingFileLogBackend>(JoinFsPaths(RootDir_, logPrefix + ".log"), 4000000, 5));
        } else {
            Log_.ResetBackend(MakeHolder<TNullLogBackend>());
        }
    }

    void TCASManager::PreprocessBlobForPut(TBlobInfo& bi, TRollbackHandler& transactionHandler, ENodeRole role) const {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_, Codec_.Get());
        processor.StartStash(*transactionHandler.TransactionLog_);

        const auto& config = Codec_->Config;
        // TODO: configure switch point for OnFS/DataInPlace
        bool compress = config.CompressOnPut && (role != ENodeRole::Result || config.CompressResults);
        NACCache::TFsBlobProcessor::TParams params = {compress ? config.Codec : CodecNone, OnFS};

        processor.PreprocessPut(params);
        bi.MutableCASHash()->SetUid(processor.GetUid());
    }

    TCASManager::TReturn TCASManager::PutBlob(const TBlobInfo& bi, i32 refCountAdj, TRollbackHandler* transactionHandler) {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_, Codec_.Get());
        if (transactionHandler) {
            processor.StartStash(*transactionHandler->TransactionLog_);
        }
        // TODO: configure switch point for OnFS/DataInPlace
        NACCache::TFsBlobProcessor::TParams params = {CodecNone, OnFS};
        return DBProcessing_.PutBlob(params, processor, refCountAdj, Log_);
    }

    EOptim TCASManager::PostprocessBlobAfterGet(const TBlobInfo& bi, TRollbackHandler& transactionHandler) {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_, Codec_.Get());
        processor.StartStash(*transactionHandler.TransactionLog_);
        // TODO: get rid of params.
        NACCache::TFsBlobProcessor::TParams params = {CodecNone, OnFS};
        auto optim = processor.PostprocessGet(params);
        if (!processor.GetPromoteFile().empty()) {
            PromoteBlob(processor.GetUid(), processor.GetPromoteFile());
        }
        return optim;
    }

    TCASManager::TReturn TCASManager::GetBlob(const TBlobInfo& bi, TRollbackHandler* transactionHandler) {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_, Codec_.Get());
        if (transactionHandler) {
            processor.StartStash(*transactionHandler->TransactionLog_);
        }
        auto result = DBProcessing_.GetBlob(processor, Log_);
        return result;
    }

    i64 TCASManager::CompressColdBlobs(const NThreading::TCancellationToken& cancel, const std::function<void(const TString& uid)>& beforeCommit) {
        const auto& config = Codec_->Config;
        if (!IsColdCompressionEnabled()) {
            return 0;
        }
        auto stash = JoinFsPaths(RootDir_, "rm", MD5::Calc("COLD"));
        NFs::RemoveRecursive(stash);
        if (!NFs::MakeDirectory(stash)) {
            ythrow TIoException() << "failed to create " << stash;
        }
        Y_DEFER {
            NFs::RemoveRecursive(stash);
        };

        // Skipped blobs stay the largest cold ones and would head every batch: known skips are not counted.
        // Links are rechecked, so the set may be dropped to bound memory.
        if (ColdLinked_.size() >= MaxColdSkips) {
            ColdLinked_.clear();
        }
        // Poor blobs are retried after a drop: bounds memory and the query limit, which counts the skips.
        if (ColdPoor_.size() >= MaxColdSkips) {
            ColdPoor_.clear();
        }
        i64 fsSizeDiff = 0;
        int selected = 0;
        int limit = config.ColdBatch + (int)ColdLinked_.size();
        i64 coldBefore = MilliSeconds() - config.ColdAge * 1000;
        for (const auto& blob : DBProcessing_.GetColdBlobs(config.MinSize, coldBefore, limit, ColdPoor_)) {
            auto path = TFsBlobProcessor::GetStoreFileName(RootDir_, blob.Uid);
            TFileStat stat(path, true);
            bool linked = !stat.IsFile() || stat.NLinks != 1;
            if (linked && ColdLinked_.contains(blob.Uid)) {
                continue;
            }
            if (cancel.IsCancellationRequested() || ++selected > config.ColdBatch) {
                break;
            }
            // Other links (symres, build outputs, running Get) keep the inode, compression frees nothing.
            if (linked) {
                Codec_->ColdSkippedLinked += stat.NLinks > 1;
                ColdLinked_.insert(blob.Uid);
                continue;
            }
            ColdLinked_.erase(blob.Uid);

            auto frame = JoinFsPaths(stash, blob.Uid);
            TMaybe<TFsBlobProcessor::TFsInfo> frameInfo;
            if (!Codec_->Encode.try_acquire()) {
                ++Codec_->ColdBusy;
                break;
            }
            try {
                Y_DEFER {
                    Codec_->Encode.release();
                };
                frameInfo = TFsBlobProcessor::CompressToFrame(path, frame, blob.Size);
            } catch (const yexception&) {
                // Removed or replaced meanwhile.
                ++Codec_->ColdRaced;
                continue;
            }
            if (!frameInfo) {
                ++Codec_->ColdPoorRatio;
                ColdPoor_.insert(blob.Uid);
                continue;
            }
            if (beforeCommit) {
                beforeCommit(blob.Uid);
            }

            auto target = TFsBlobProcessor::GetStoreFileName(RootDir_, blob.Uid, CodecZstd);
            bool renamed = false;
            TMaybe<i64> diff;
            try {
                diff = DBProcessing_.ChangeCodec(blob.Uid, CodecNone, CodecZstd, [&](i64& fsSize, i64& mode) {
                    // Get links the file under DB lock, a copying Get is caught by coldBefore.
                    TFileStat stat(path, true);
                    if (!stat.IsFile() || stat.NLinks != 1) {
                        return false;
                    }
                    if (!NFs::Rename(frame, target)) {
                        ythrow TIoException() << "Cannot rename " << frame << " to " << target;
                    }
                    renamed = true;
                    fsSize = frameInfo->FSSize;
                    mode = stat.Mode;
                    return true;
                }, FSSizeObserver_, coldBefore);
            } catch (...) {
                if (renamed) {
                    NFs::Remove(target);
                }
                throw;
            }
            if (!diff) {
                ++Codec_->ColdRaced;
                continue;
            }
            fsSizeDiff += *diff;
            ++Codec_->ColdCompressed;
            Codec_->ColdSavedBytes -= *diff;
            RemovePlainLeftover(blob.Uid);
        }
        return fsSizeDiff;
    }

    TFsBlobProcessor::TOrphanStats TCASManager::CleanupOrphans(TDuration minAge) {
        if (!IsBlobIOEnabled()) {
            return {};
        }
        auto unlessRow = [this](const TString& uid, const std::function<void()>& action) {
            return DBProcessing_.RemoveUnlessCodec(uid, CodecZstd, [&action]() {
                action();
                return true;
            });
        };
        auto removePlain = [this](const TString& uid) {
            return RemovePlainLeftover(uid);
        };
        return TFsBlobProcessor::RemoveOrphanFrames(RootDir_, DBProcessing_.GetZstdBlobUids(), unlessRow, minAge, removePlain);
    }

    bool TCASManager::RemovePlainLeftover(const TString& uid) {
        auto path = TFsBlobProcessor::GetStoreFileName(RootDir_, uid);
        return DBProcessing_.RemoveUnlessCodec(uid, CodecNone, [&path]() {
            return NFs::Remove(path);
        });
    }

    i64 TCASManager::PromoteBlob(const TString& uid, const TString& file) {
        auto plain = TFsBlobProcessor::GetStoreFileName(RootDir_, uid);
        bool renamed = false;
        bool modeChanged = false;
        i64 size = 0;
        TMaybe<i64> diff;
        try {
            diff = DBProcessing_.ChangeCodec(uid, CodecZstd, CodecNone, [&](i64& fsSize, i64& mode) {
                // Target could be changed after decompression, it must be the blob with the same uid.
                TFileStat stat(file, true);
                if (!stat.IsFile() || (stat.Mode & 0777) != (mode & 0777)) {
                    modeChanged = stat.IsFile();
                    return false;
                }
                if (NFs::Exists(plain) || !NFs::Rename(file, plain)) {
                    return false;
                }
                renamed = true;
                size = stat.Size;
                fsSize = TFsBlobProcessor::GetStatInfo(plain).FSSize;
                return true;
            }, FSSizeObserver_);
        } catch (...) {
            if (renamed) {
                NFs::Remove(plain);
            }
            throw;
        }
        if (!diff) {
            ++(modeChanged ? Codec_->PromoteSkipped : Codec_->PromoteRaced);
            NFs::Remove(file);
            return 0;
        }
        ++Codec_->Promoted;
        Codec_->PromotedBytes += size;
        auto frame = TFsBlobProcessor::GetStoreFileName(RootDir_, uid, CodecZstd);
        DBProcessing_.RemoveUnlessCodec(uid, CodecZstd, [&frame]() {
            return NFs::Remove(frame);
        });
        return *diff;
    }

    TString TCASManager::GetRelativePath(const TString& rootDir, const TString& fullPath) const {
        return TFsPath(fullPath).RelativeTo(rootDir);
    }

    TString TCASManager::CreateNumberedDir(const TString& dir, const char* digit) {
        auto numberedDir = JoinFsPaths(dir, digit);
        if (!TFileStat(numberedDir).IsDir()) {
            NFs::MakeDirectory(numberedDir);
            if (!TFileStat(numberedDir).IsDir()) {
                ythrow TIoException() << "failed to create " << numberedDir;
            }
        }
        return numberedDir;
    }

    TRollbackHandler::TRollbackHandler(TCASManager& parent, const TString& tid, bool sync)
        : Parent_(&parent)
    {
        Y_ABORT_UNLESS(Parent_);
        TransactionLog_.Reset(TFsBlobProcessor::GetTransactionLog(&Parent_->RootDir_, tid, Parent_->Mode_, sync).Release());
    }

    TRollbackHandler::~TRollbackHandler() noexcept {
        if (TransactionLog_.Get()) {
            try {
                Rollback();
            } catch (const std::exception& e) {
                try {
                    LOGGER_CHECKED_GENERIC_LOG(Parent_->GetLog(), TRTYLogPreprocessor, TLOG_ERR, "ERR[ACCAS]")
                        << "Rollback cleanup failed: " << e.what() << Endl;
                } catch (...) {
                }
            } catch (...) {
                try {
                    LOGGER_CHECKED_GENERIC_LOG(Parent_->GetLog(), TRTYLogPreprocessor, TLOG_ERR, "ERR[ACCAS]")
                        << "Rollback cleanup failed with an unknown exception" << Endl;
                } catch (...) {
                }
            }
        }
    }

    void TRollbackHandler::Rollback() {
        Y_ABORT_UNLESS(TransactionLog_.Get());
        Y_ABORT_UNLESS(Parent_);

        TransactionLog_->Rollback(Parent_->Mode_);
        TransactionLog_.Reset(nullptr);
        Parent_ = nullptr;
    }

    void TRollbackHandler::Commit() {
        Y_ABORT_UNLESS(TransactionLog_.Get());
        Y_ABORT_UNLESS(Parent_);

        TransactionLog_->Commit(Parent_->Mode_);
        TransactionLog_.Reset(nullptr);
        Parent_ = nullptr;
    }
}
