#include "cas.h"

#include <library/cpp/logger/null.h>
#include <library/cpp/logger/rotating_file.h>

#include <util/generic/hash_set.h>

namespace NACCache {
    using namespace NSQLite;

    bool operator==(const THash& a, const THash& b) {
        return a.GetUid() == b.GetUid();
    }
}

namespace NACCache {
    using namespace NSQLite;
    TCASManager::TCASManager(NSQLite::TSQLiteDB& db, const TString& rootDir, bool nested, TFsBlobProcessor::EOperationMode mode, bool enableLogging, const TString& logPrefix, bool recreate)
        : DBProcessing_(db, nested)
        , RootDir_(rootDir)
        , Mode_(mode)
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

    void TCASManager::PreprocessBlobForPut(TBlobInfo& bi, TRollbackHandler& transactionHandler) const {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_);
        processor.StartStash(*transactionHandler.TransactionLog_);

        // TODO: configure switch point for OnFS/DataInPlace
        NACCache::TFsBlobProcessor::TParams params = {CodecNone, OnFS};

        processor.PreprocessPut(params);
        bi.MutableCASHash()->SetUid(processor.GetUid());
    }

    TCASManager::TReturn TCASManager::PutBlob(const TBlobInfo& bi, i32 refCountAdj, TRollbackHandler* transactionHandler) {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_);
        if (transactionHandler) {
            processor.StartStash(*transactionHandler->TransactionLog_);
        }
        // TODO: configure switch point for OnFS/DataInPlace
        NACCache::TFsBlobProcessor::TParams params = {CodecNone, OnFS};
        return DBProcessing_.PutBlob(params, processor, refCountAdj, Log_);
    }

    EOptim TCASManager::PostprocessBlobAfterGet(const TBlobInfo& bi, TRollbackHandler& transactionHandler) const {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_);
        processor.StartStash(*transactionHandler.TransactionLog_);
        // TODO: get rid of params.
        NACCache::TFsBlobProcessor::TParams params = {CodecNone, OnFS};
        return processor.PostprocessGet(params);
    }

    TCASManager::TReturn TCASManager::GetBlob(const TBlobInfo& bi, TRollbackHandler* transactionHandler) {
        TFsBlobProcessor processor(&bi, &RootDir_, Mode_);
        if (transactionHandler) {
            processor.StartStash(*transactionHandler->TransactionLog_);
        }
        auto result = DBProcessing_.GetBlob(processor, Log_);
        return result;
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

    TRollbackHandler::~TRollbackHandler() {
        if (TransactionLog_.Get()) {
            Rollback();
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
