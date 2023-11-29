#pragma once

#include "db-public.h"
#include "devtools/local_cache/ac/fs/fs_blobs.h"

#include <library/cpp/logger/log.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/generic/string.h>
#include <util/generic/list.h>

namespace NACCache {
    bool operator==(const THash& a, const THash& b);

    inline bool operator!=(const THash& a, const THash& b) {
        return !(a == b);
    }

    class TRollbackHandler : TMoveOnly {
    public:
        friend class TCASManager;

        ~TRollbackHandler();

        void Commit();

        void Rollback();

    private:
        TRollbackHandler(TCASManager& parent, const TString& tid, bool sync);

        TCASManager* Parent_;
        // Put/Get transaction support
        THolder<TFsBlobProcessor::TTransactionLog> TransactionLog_;
    };

    class TCASManager : TNonCopyable {
    public:
        friend class TRollbackHandler;

        using TReturn = NACCachePrivate::TCASStmts::TReturn;

        TCASManager(NSQLite::TSQLiteDB& db, const TString& rootDir, bool nested, TFsBlobProcessor::EOperationMode mode, bool enableLogging, const TString& logPrefix, bool recreate);

        void PreprocessBlobForPut(TBlobInfo&, TRollbackHandler& transactionHandler) const;
        EOptim PostprocessBlobAfterGet(const TBlobInfo&, TRollbackHandler& transactionHandler) const;

        TReturn PutBlob(const TBlobInfo&, i32 refCountAdj, TRollbackHandler* transactionHandler);
        TReturn GetBlob(const TBlobInfo&, TRollbackHandler* transactionHandler);

        TString GetRelativePath(const TString& rootPath, const TString& fullPath) const;

        TRollbackHandler GetRollbackGuard(const TString& tid, bool sync) {
            return TRollbackHandler(*this, tid, sync);
        }

        TLog& GetLog() {
            return Log_;
        }

    private:
        static THolder<TFsBlobProcessor::TTransactionLog> StartPutGetTransaction(const TString& tid);

        static TString CreateNumberedDir(const TString& dir, const char* digit);

        NACCachePrivate::TCASStmts DBProcessing_;
        TString RootDir_;
        TLog Log_;

        TFsBlobProcessor::EOperationMode Mode_;
    };
}
