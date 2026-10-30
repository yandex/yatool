#pragma once

#include "devtools/local_cache/ac/proto/ac.pb.h"

#include <library/cpp/openssl/crypto/sha.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/maybe.h>
#include <util/generic/set.h>
#include <util/string/hex.h>
#include <util/datetime/base.h>
#include <util/system/file.h>
#include <util/system/fs.h>

#include <atomic>
#include <functional>
#include <semaphore>

namespace NACCache {
    void SetSharedMode(bool enabled);
    bool IsSharedMode();

    enum ECodec {
        CodecNone,
        CodecZstd,
        // Written by a newer server, cannot be read.
        CodecUnknown
    };

    /// Value of blobs.codec column.
    TStringBuf CodecName(ECodec codec);
    ECodec CodecFromName(TStringBuf name);

    /// [local_cache.build_cache] codec* settings.
    struct TCodecConfig {
        ECodec Codec = CodecNone;
        i64 MinSize = 1 << 20;
        bool CompressResults = true;
        bool CompressOnPut = true;
        /// Get with hardlink optimization of compressed blob makes it plain again.
        bool PromoteOnGet = true;
        int MaxParallelEncode = 4;
        int MaxParallelDecode = 8;
        /// Background compression of blobs not accessed for ColdAge seconds, 0 disables it.
        i64 ColdAge = 0;
        i64 ColdInterval = 600;
        int ColdBatch = 200;
    };

    /// Compression limits and counters shared by all processors of a store.
    struct TCodecState : TNonCopyable {
        explicit TCodecState(const TCodecConfig& config)
            : Config(config)
            , Encode(Max(config.MaxParallelEncode, 0))
            , Decode(Max(config.MaxParallelDecode, 1))
        {
        }

        const TCodecConfig Config;
        std::counting_semaphore<> Encode;
        std::counting_semaphore<> Decode;

        std::atomic<i64> Compressed = 0;
        std::atomic<i64> SyncFallbacks = 0;
        std::atomic<i64> PoorRatio = 0;
        std::atomic<i64> FoundInStore = 0;
        std::atomic<i64> EncodeBusy = 0;
        std::atomic<i64> Decoded = 0;

        std::atomic<i64> ColdCompressed = 0;
        std::atomic<i64> ColdSavedBytes = 0;
        std::atomic<i64> ColdSkippedLinked = 0;
        std::atomic<i64> ColdPoorRatio = 0;
        std::atomic<i64> ColdBusy = 0;
        std::atomic<i64> ColdRaced = 0;

        std::atomic<i64> Promoted = 0;
        std::atomic<i64> PromotedBytes = 0;
        std::atomic<i64> PromoteSkipped = 0;
        std::atomic<i64> PromoteRaced = 0;
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
            // Stored representation, Size and Mode are of the original file.
            ECodec Codec = CodecNone;
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

            void AddRemoveFile(const TString& fileHash, ECodec codec) {
                RemoveFiles_.insert(fileHash);
                RemovedCodecs_[fileHash] = codec;
            }

            void AddPreprocessPutOrGetResult(const TString& fileHash, const TString& content, EOptim optim, const TFsInfo& info) {
                PreprocessPutOrGetResults_[fileHash] = std::make_tuple(content, optim, info);
            }

            void AddGetResult(const TString& fileHash, EOptim optim) {
                GetResults_[fileHash] = optim;
            }

            void PinFrame(const TString& fileHash, TFile frame) {
                PinnedFrames_.emplace(fileHash, std::move(frame));
            }

            const TFile* GetPinnedFrame(const TString& fileHash) const {
                auto it = PinnedFrames_.find(fileHash);
                return it == PinnedFrames_.end() ? nullptr : &it->second;
            }

            TString GetStashDir(EFileState state);

            TString GetStashedName(const TString& fileName, EFileState state);
            ECodec GetStashedCodec(const TString& fileHash, EFileState state) const;

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
            THashMap<TString, ECodec> RemovedCodecs_;

            // Result returned from TFsBlobProcessor::PreprocessPut or from TFsBlobProcessor::Get
            // The third component is TFsInfo for InStore
            THashMap<TString, std::tuple<TString, EOptim, TFsInfo>> PreprocessPutOrGetResults_;
            // Result returned from TFsBlobProcessor::Get
            THashMap<TString, EOptim> GetResults_;
            // Frames opened by Get in sync mode, which has no stash to link them to.
            THashMap<TString, TFile> PinnedFrames_;

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
        TFsBlobProcessor(const TBlobInfo* blobInfo, const TString* rootDir, EOperationMode mode, TCodecState* codec = nullptr);

        /// Sync part to be performed under DB lock
        std::tuple<TString, EOptim> Put(const TParams& params, TFsInfo& info);
        /// Async part of PUt's IO, copy/hardlink/rename to stash directory, compute hash if needed
        /// params.Codec requests compression, info.Codec returned from Put tells if it was applied.
        void PreprocessPut(const TParams& params);

        /// Sync part to be performed under DB lock
        /// Parameters are unused if params.Mode == OnFS
        /// size is the original size from DB, checked for compressed blobs.
        EOptim Get(const TParams& params, TStringBuf content, i64 outStoreMode, i64 size = -1);
        /// Async part of Get's IO, copy/hardlink/rename from stash directory
        /// TODO: Get rid of params
        EOptim PostprocessGet(const TParams& params);

        /// No-op if params.Mode != OnFS
        void RemoveBlob(const TParams& params, TFsInfo& info);

        /// True if blob is expected on FS but neither in store (plain or zstd) nor put in current transaction (lstat only).
        bool IsMissingInStore(const TParams& params);

        /// Get file information in or out of store.
        /// Obvious requirements wrt Put/Get
        TFsInfo GetInfo(EFilePlacement placement);

        /// Uid can be computed for Put-operation.
        const TString& GetUid();

        EOptim GetRequestedOptimization() const {
            return BlobInfo_.GetOptimization();
        }

        /// Link of decompressed Get target to promote, set by PostprocessGet.
        const TString& GetPromoteFile() const {
            return PromoteFile_;
        }

        /// Special processing of removal as it is hard to rollback afterward.
        /// Stash removed files in stashDir
        void StartStash(TTransactionLog& transactionLog);

        /// Returns TTransactionLog to be used in corresponding (rootDir == RootDir_) StartStash
        static THolder<TTransactionLog> GetTransactionLog(const TString* rootDir, const TString& tid, EOperationMode mode, bool sync);

        struct TOrphanStats {
            size_t Removed = 0;
            size_t Fresh = 0;
            size_t Kept = 0;
            size_t PlainRemoved = 0;
        };

        /// Removes <uid>.zst files older than minAge that have neither uid in known nor a row.
        /// unlessRow(uid, action) runs action, which moves the frame aside, unless a row exists and returns whether it ran:
        /// the check and the move must be atomic with commits that put a frame in place.
        /// Plain <uid> files of known uids with a single link and older than minAge are passed to removePlain,
        /// it returns true if the file was removed.
        static TOrphanStats RemoveOrphanFrames(const TString& storeRoot, const THashSet<TString>& known,
                                               const std::function<bool(const TString& uid, const std::function<void()>& action)>& unlessRow, TDuration minAge,
                                               const std::function<bool(const TString&)>& removePlain = {});

    private:
        // Refinement of EOperationMode accounting for EBlobStoreMode
        enum EIOMode {
            NoIOOps,   /// No IO operations at all Mode_ == NoIO
            NoStoreIO, /// EBlobStoreMode != OnFS && Mode_ == Regular
            FullIO     /// EBlobStoreMode == OnFS && Mode_ == Regular
        };

        /// Full path including RootDir_, need computed uid.
        TString GetStoreFileName(ECodec codec = CodecNone);

        /// Full path including RootDir_, need computed uid.
        static TString GetStoreFileName(const TString& storeRoot, const TString& baseBlobName, ECodec codec = CodecNone);

        /// Compresses OutStore file to stash, returns false if the regular path should be used.
        bool TryCompressForPut(TFsInfo& info);

        /// Compresses from to frame of the original size. Keeps the frame and returns its info if it saves enough.
        static TMaybe<TFsInfo> CompressToFrame(const TString& from, const TString& frame, i64 size);

        void MakeTargetDir() const;

        /// Decompresses to a temporary file next to target, checks and renames it.
        void DecompressBlob(const TFile& source, const TString& target, const TFsInfo& dbInfo) const;
        /// Same without a Decode slot: waiting for one under DB lock would stop the daemon, and the lock serializes these anyway.
        void DecompressBlobUnderLock(const TFile& source, const TString& target, const TFsInfo& dbInfo) const;

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
        void CopyBlob(const TFsPath& from, const TFsPath& to, i64 fileMode, bool sharedOutput) const;

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

        TCodecState* Codec_;

        TString PromoteFile_;
    };
}
