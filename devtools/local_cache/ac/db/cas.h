#pragma once

#include "db-public.h"
#include "devtools/local_cache/ac/fs/fs_blobs.h"

#include <library/cpp/logger/log.h>
#include <library/cpp/threading/cancellation/cancellation_token.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/generic/hash_set.h>
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

        ~TRollbackHandler() noexcept;

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

        TCASManager(NSQLite::TSQLiteDB& db, const TString& rootDir, bool nested, TFsBlobProcessor::EOperationMode mode, bool enableLogging, const TString& logPrefix, bool recreate, const TCodecConfig& codec = {});

        enum class ENodeRole {
            Intermediate,
            Result,
        };

        /// Compression per codec config: codec_on_put, codec_results for result nodes.
        void PreprocessBlobForPut(TBlobInfo&, TRollbackHandler& transactionHandler, ENodeRole role = ENodeRole::Intermediate) const;
        /// Promotes decompressed blob back to plain per codec_promote_on_get.
        EOptim PostprocessBlobAfterGet(const TBlobInfo&, TRollbackHandler& transactionHandler);

        TReturn PutBlob(const TBlobInfo&, i32 refCountAdj, TRollbackHandler* transactionHandler);
        TReturn GetBlob(const TBlobInfo&, TRollbackHandler* transactionHandler);

        TString GetRelativePath(const TString& rootPath, const TString& fullPath) const;

        TRollbackHandler GetRollbackGuard(const TString& tid, bool sync) {
            return TRollbackHandler(*this, tid, sync);
        }

        TLog& GetLog() {
            return Log_;
        }

        /// Gets FSSize diff of every committed codec change (promote, cold compression) under the DB lock:
        /// the request may still fail, and a statistics snapshot under the DB lock sees both or neither.
        void SetFSSizeObserver(std::function<void(i64)> observer) {
            FSSizeObserver_ = std::move(observer);
        }

        bool IsBlobIOEnabled() const {
            return Mode_ != TFsBlobProcessor::NoIO;
        }

        bool IsColdCompressionEnabled() const {
            return IsBlobIOEnabled() && Codec_->Config.Codec != CodecNone && Codec_->Config.ColdAge > 0;
        }

        TDuration GetColdCompressionInterval() const {
            return TDuration::Seconds(Codec_->Config.ColdInterval);
        }

        const TCodecState& GetCodecState() const {
            return *Codec_;
        }

        /// Compresses up to codec_cold_batch blobs not accessed for codec_cold_age, see TCodecConfig.
        /// Encoding is done outside of DB lock, beforeCommit is a test hook called after it.
        /// \return FSSize diff, already passed to the observer.
        i64 CompressColdBlobs(const NThreading::TCancellationToken& cancel = NThreading::TCancellationToken::Default(), const std::function<void(const TString& uid)>& beforeCommit = {});

        /// Removes orphan frames and plain leftovers of compressed blobs older than minAge.
        TFsBlobProcessor::TOrphanStats CleanupOrphans(TDuration minAge);

        /// Removes plain <uid> file left by CompressColdBlobs unless a plain row needs it.
        bool RemovePlainLeftover(const TString& uid);

        /// Makes decompressed file (a link of Get target) the plain blob in a transaction and removes the frame.
        /// \return FSSize diff, already passed to the observer.
        i64 PromoteBlob(const TString& uid, const TString& file);

    private:
        static THolder<TFsBlobProcessor::TTransactionLog> StartPutGetTransaction(const TString& tid);

        static TString CreateNumberedDir(const TString& dir, const char* digit);

        NACCachePrivate::TCASStmts DBProcessing_;
        TString RootDir_;
        TLog Log_;

        TFsBlobProcessor::EOperationMode Mode_;
        THolder<TCodecState> Codec_;
        std::function<void(i64)> FSSizeObserver_;

        /// Uids skipped by CompressColdBlobs (used by one thread): poor ratio is final, links are rechecked.
        THashSet<TString> ColdPoor_;
        THashSet<TString> ColdLinked_;
    };
}
