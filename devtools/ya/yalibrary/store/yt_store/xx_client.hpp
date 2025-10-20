#pragma once

#include <yt/cpp/mapreduce/interface/client.h>

#include <util/folder/path.h>

struct YtStoreClientResponse {
    bool Success;
    bool NetworkErrors;
    size_t DecodedSize;
    char ErrorMsg[4096];
};

struct YtStoreClientRequest {
    const char* Hash;
    const char* IntoDir;
    const char* Codec;
    size_t DataSize;
    int Chunks;
};

struct YtStorePrepareDataRequest {
    const char* OutPath;
    const char* Codec;
    const char* RootDir;
    TVector<const char*> Files;
};

struct YtStorePrepareDataResponse {
    bool Success;
    size_t RawSize;
    char ErrorMsg[4096];
};

struct YtStore {
    YtStore(const char* yt_proxy, const char* yt_dir, const char* yt_token, TDuration retry_time_limit = {});
    ~YtStore();
    void DoTryRestore(const YtStoreClientRequest& req, YtStoreClientResponse& rsp);
    void PrepareData(const YtStorePrepareDataRequest& req, YtStorePrepareDataResponse& rsp);
    NYT::IClientPtr Client;
    std::string YtDir;
};

namespace NYa {
    void AtExit();

    struct TNameReTtl {
        TString NameRe;
        TDuration Ttl;
    };

    struct TYtTokenOption {
        TString Token{};
    };

    enum class ECritLevel {
        NONE,
        GET,
        PUT
    };

    using TMaxCacheSize = std::variant<size_t, double>;
    struct TYtStore2Options : public TYtTokenOption {
        TYtStore2Options() = default;

        void* Owner{};
        bool ReadOnly{true};
        bool CheckSize{};
        TMaxCacheSize MaxCacheSize{0u};
        TDuration Ttl{};
        TVector<TNameReTtl> NameReTtls{};
        TString OperationPool{};
        TDuration RetryTimeLimit{};
        TDuration InitTimeout{};
        TDuration PrepareTimeout{};
        bool ProbeBeforePut{};
        size_t ProbeBeforePutMinSize{};
        ECritLevel CritLevel{ECritLevel::NONE};
    };

    struct TYtStoreError : public yexception {
        TYtStoreError(bool mute = false)
            : Mute{mute}
        {
        }

        static inline TYtStoreError Muted() {
            return TYtStoreError(true);
        }

        bool Mute{};
    };

    class TYtStore2 {
    public:
        using TUidList = TVector<TString>;

        struct TPrepareOptions : public TThrRefBase {
            TUidList SelfUids{};
            TUidList Uids{};
            bool RefreshOnRead{};
            bool ContentUidsEnabled{};
        };
        using TPrepareOptionsPtr = TIntrusivePtr<TPrepareOptions>;

        struct TPutOptions {
            TString SelfUid{};
            TString Uid{};
            TFsPath RootDir{};
            TVector<TFsPath> Files{};
            TString Codec{};
            TString Cuid{};
            size_t ForcedSize{};
        };

        struct TDataGcOptions {
            i64 DataSizePerJob{};
            ui64 DataSizePerKeyRange{};
        };

        struct TMetrics {
            THashMap<TString, TDuration> Timers{};
            THashMap<TString, TVector<std::pair<TInstant, TInstant>>> TimerIntervals{};
            THashMap<TString, int> Counters{};
            THashMap<TString, int> Failures{};
            THashMap<TString, size_t> DataSize{};
            // Cache hit
            int Requested{};
            int Found{};
            // CompressionRatio
            size_t TotalCompressedSize{};
            size_t TotalRawSize{};
            // Additional metrics
            TInstant TimeToFirstCallHas{};
            TInstant TimeToFirstRecvMeta{};
        };

        struct TCreateTablesOptions : public TYtTokenOption {
            unsigned Version{};
            bool Replicated{};
            bool Tracked{};
            bool InMemory{};
            bool Mount{};
            bool IgnoreExisting{};
            std::optional<ui64> MetadataTabletCount{};
            std::optional<ui64> DataTabletCount{};
        };

        struct TModifyTablesStateOptions : public TYtTokenOption {
            enum EAction {
                MOUNT,
                UNMOUNT
            };

            EAction Action;
        };

        struct TModifyReplicaOptions : public TYtTokenOption {
            enum EAction {
                CREATE,
                REMOVE
            };

            EAction Action{CREATE};
            std::optional<bool> SyncMode{};
            std::optional<bool> Enable{};
        };

    public:
        TYtStore2(const TString& proxy, const TString& dataDir, const TYtStore2Options& options);
        ~TYtStore2();

        bool Disabled() const noexcept;
        bool ReadOnly() const noexcept;
        void Prepare(TPrepareOptionsPtr options);
        bool Has(const TString& uid);
        bool TryRestore(const TString& uid, const TString& intoDir);
        bool Put(const TPutOptions& options);
        TMetrics GetMetrics() const;
        void Strip();
        void DataGc(const TDataGcOptions& options);
        void Shutdown() noexcept;

        static void ValidateRegexp(const TString& re);
        static void CreateTables(const TString& proxy, const TString& dataDir, const TCreateTablesOptions& options);
        static void ModifyTablesState(const TString& proxy, const TString& dataDir, const TModifyTablesStateOptions& options);
        static void ModifyReplica(const TString& proxy, const TString& dataDir, const TString& replicaProxy, const TString& replicaDataDir, const TModifyReplicaOptions& options);
    private:
        class TImpl;
        std::unique_ptr<TImpl> Impl_;
    };
}
