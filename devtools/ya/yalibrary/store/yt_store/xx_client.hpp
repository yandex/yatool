#pragma once

#include <yt/cpp/mapreduce/interface/client.h>

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
    struct TNameReTtl {
        TString NameRe;
        TDuration Ttl;
    };

    using TMaxCacheSize = std::variant<size_t, double>;
    struct TYtStore2Options {
        TYtStore2Options() = default;

        TString Token;
        bool ReadOnly{true};
        void* OnDisable{};
        TMaxCacheSize MaxCacheSize{0u};
        TDuration Ttl{};
        TVector<TNameReTtl> NameReTtls{};
        TString OperationPool{};
        TDuration RetryTimeLimit{};
        bool SyncDurability{};
    };

    struct TDataGcOptions {
        i64 DataSizePerJob{};
        ui64 DataSizePerKeyRange{};
    };

    class TYtStore2 {
    public:
        TYtStore2(const TString& proxy, const TString& dataDir, const TYtStore2Options& options);
        ~TYtStore2();

        void WaitInitialized();
        bool Disabled() const;
        void Strip();
        void DataGc(const TDataGcOptions& options);

        static void ValidateRegexp(const TString& re);
    private:
        class TImpl;
        std::unique_ptr<TImpl> Impl_;
    };
}
