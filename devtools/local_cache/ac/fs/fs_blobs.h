#pragma once

#include "devtools/local_cache/ac/proto/ac.pb.h"

#include <library/cpp/openssl/crypto/sha.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/set.h>
#include <util/string/hex.h>
#include <util/system/fs.h>

namespace NACCache {
    enum ECodec {
        CodecNone
    };

    constexpr size_t DIGEST_LENGTH = NOpenSsl::NSha1::DIGEST_LENGTH;
    using TDigest = NOpenSsl::NSha1::TDigest;
    using TCalcer = NOpenSsl::NSha1::TCalcer;

    constexpr NFs::EFilePermission StoragePermissions = NFs::FP_SECRET_FILE;

    class TCASManager;

    /// Class to process blobs stored on FS
    class TFsBlobProcessor : TNonCopyable {
    public:
        struct TFsInfo {
            i64 Size = 0;
            i64 FSSize = 0;
            i64 Mode = 0;
            bool operator==(const TFsInfo& other) const {
                return Size == other.Size && FSSize == other.FSSize && Mode == other.Mode;
            }
        };

        enum EFilePlacement {
            InStore,
            OutStore
        };

        enum EOperationMode {
            Regular,
            // Benchmark mode
            NoIO,
        };

        /// Parameters from DB or for DB.
        struct TParams {
            ECodec Codec = CodecNone;
            EBlobStoreMode Mode = OnFS;
        };

        /// Groups several TFsBlobProcessor of the same kind (Put, Get or Remove then Put) into one operation with
        /// commit/rollback.
        /// Get cannot be mixed with Put or Remove
        class TTransactionLog {
        public:
            friend class TFsBlobProcessor;

            ~TTransactionLog();

            /// Special processing of removal as it is hard to rollback afterward.
            /// Bulk processing from multiple TFsBlobProcessors'
            void Rollback(EOperationMode mode);

            /// Special processing of removal as it is hard to rollback afterward.
            /// Bulk processing from multiple TFsBlobProcessors'
            void Commit(EOperationMode mode);

        private:
            enum EFileState {
                OldFile,
                NewFile
            };

            TTransactionLog(bool sync, const TString& stashDir, const TString* storeRoot, EOperationMode mode)
                : StashDir_(stashDir)
                , StoreRoot_(*storeRoot)
                , Sync_(sync)
                , DirectoriesPrepared_(false)
            {
                if (!Sync_ && mode == Regular) {
                    // Can prepare directories immediately
                    CreateStashDirs();
                }
            }

            bool IsSynchronous() const {
                return Sync_;
            }

            std::pair<const TString, EOptim> PreprocessPutOrGetResult(const TString& fileHash, TFsInfo& info) const {
                auto tuple = PreprocessPutOrGetResults_.find(fileHash)->second;
                info = std::get<2>(tuple);
                return std::make_pair(std::get<0>(tuple), std::get<1>(tuple));
            }

            bool HasPreprocesedPutOrGetResult(const TString& fileHash) {
                return PreprocessPutOrGetResults_.find(fileHash) != PreprocessPutOrGetResults_.end();
            }

            EOptim GetResult(const TString& fileHash) const {
                return GetResults_.find(fileHash)->second;
            }

            void AddPreparedPutFile(const TString& fileHash) {
                PreparedPutFiles_.insert(fileHash);
            }

            void AddPutFile(const TString& fileHash) {
                PreparedPutFiles_.erase(fileHash);
                PutFiles_.insert(fileHash);
            }

            void AddGetFile(const TString& filePath) {
                NewGetFiles_.insert(filePath);
            }

            void AddRemoveFile(const TString& fileHash) {
                RemoveFiles_.insert(fileHash);
            }

            void AddPreprocessPutOrGetResult(const TString& fileHash, const TString& content, EOptim optim, const TFsInfo& info) {
                PreprocessPutOrGetResults_[fileHash] = std::make_tuple(content, optim, info);
            }

            void AddGetResult(const TString& fileHash, EOptim optim) {
                GetResults_[fileHash] = optim;
            }

            TString GetStashDir(EFileState state);

            TString GetStashedName(const TString& fileName, EFileState state);

            void RemoveBlobs(const TSet<TString>& remove, EFileState state);
            void RenameBlobs(const TSet<TString>& rename, EFileState state);
            void CleanupStashDirs();
            void CreateStashDirs();

            // New files from Get.
            TSet<TString> NewGetFiles_;
            // Files prepared for Put.
            TSet<TString> PreparedPutFiles_;
            // Files actually Put.
            TSet<TString> PutFiles_;
            // Files removed
            TSet<TString> RemoveFiles_;

            // Result returned from TFsBlobProcessor::PreprocessPut or from TFsBlobProcessor::Get
            // The third component is TFsInfo for InStore
            THashMap<TString, std::tuple<TString, EOptim, TFsInfo>> PreprocessPutOrGetResults_;
            // Result returned from TFsBlobProcessor::Get
            THashMap<TString, EOptim> GetResults_;

            // Transactional removal of files
            TString StashDir_;
            const TString& StoreRoot_;
            bool Sync_;
            bool DirectoriesPrepared_;
        };

        friend class TCASManager;

        /// rootDir points to parent directory of storage on FS.
        ///
        /// Pointers are to make sure no temporary copies created.
        TFsBlobProcessor(const TBlobInfo* blobInfo, const TString* rootDir, EOperationMode mode);

        /// Sync part to be performed under DB lock
        std::tuple<TString, EOptim> Put(const TParams& params, TFsInfo& info);
        /// Async part of PUt's IO, copy/hardlink/rename to stash directory, compute hash if needed
        void PreprocessPut(const TParams& params);

        /// Sync part to be performed under DB lock
        /// Parameters are unused if params.Mode == OnFS
        EOptim Get(const TParams& params, TStringBuf content, i64 outStoreMode);
        /// Async part of Get's IO, copy/hardlink/rename from stash directory
        /// TODO: Get rid of params
        EOptim PostprocessGet(const TParams& params);

        /// No-op if params.Mode != OnFS
        void RemoveBlob(const TParams& params, TFsInfo& info);

        /// Get file information in or out of store.
        /// Obvious requirements wrt Put/Get
        TFsInfo GetInfo(EFilePlacement placement);

        /// Uid can be computed for Put-operation.
        const TString& GetUid();

        EOptim GetRequestedOptimization() const {
            return BlobInfo_.GetOptimization();
        }

        /// Special processing of removal as it is hard to rollback afterward.
        /// Stash removed files in stashDir
        void StartStash(TTransactionLog& transactionLog);

        /// Returns TTransactionLog to be used in corresponding (rootDir == RootDir_) StartStash
        static THolder<TTransactionLog> GetTransactionLog(const TString* rootDir, const TString& tid, EOperationMode mode, bool sync);

    private:
        // Refinement of EOperationMode accounting for EBlobStoreMode
        enum EIOMode {
            NoIOOps,   /// No IO operations at all Mode_ == NoIO
            NoStoreIO, /// EBlobStoreMode != OnFS && Mode_ == Regular
            FullIO     /// EBlobStoreMode == OnFS && Mode_ == Regular
        };

        /// Full path including RootDir_, need computed uid.
        TString GetStoreFileName();

        /// Full path including RootDir_, need computed uid.
        static TString GetStoreFileName(const TString& storeRoot, const TString& baseBlobName);

        /// InStore placement needs uid.
        TString GetFileName(EFilePlacement placement) {
            return placement == OutStore ? BlobInfo_.GetPath() : GetStoreFileName();
        }

        /// Get temporary directory for stashing
        /// Stash dir can be shared by different TFsBlobProcessor in AC processing.
        ///
        /// Second returned value signals if synchronous processing is requested, i.e. no IO can be
        /// can be performed in advance.
        static std::pair<TString, bool> PrepareStashDir(const TString& rootDir, const TString& tid, EOperationMode mode, bool sync);

        bool IsProcessingSplit() const {
            return TransactionLog_ && !TransactionLog_->IsSynchronous();
        }

        EIOMode GetIOMode(const TParams& params) const {
            return Mode_ == Regular ? (params.Mode != OnFS ? NoStoreIO : FullIO) : NoIOOps;
        }

        /// Common processing for Put and PreprocessPut
        std::tuple<TString, EOptim> PutInternal(const TParams& params, TFsInfo& info);

        /// Common processing for Get and PostprocessGet
        EOptim GetInternal(const TParams& params, EOptim optim, const TString& sourceFile, const TString& targetFile, TStringBuf content, i64 outStoreMode);

        /// Uid can be computed for files on FS (placement == OutStore)
        /// placement == InStore is only suitable for hash verification.
        TString GetUid(EFilePlacement placement);

        /// Copy content with permissions
        void CopyBlob(const TFsPath& from, const TFsPath& to, i64 fileMode) const;

        /// throws exception on error
        void CheckFile(TStringBuf content, const TFsPath& toFileName) const;

        /// throws exception on error
        void CheckFile(const TFsPath& fromFileName, const TFsPath& toFileName, i64 size) const;

        /// Utility to compute stats after Put/Get
        static TFsInfo GetStatInfo(const TString& target);

        constexpr static int DIGEST_CHECK_SIZE = 4096;

        TMaybe<TString> SUid_;
        const TBlobInfo& BlobInfo_;
        const TString& RootDir_;

        // Transactional removal of files
        TTransactionLog* TransactionLog_;

        // Benchmark mode/regular mode
        EOperationMode Mode_;
    };
}
