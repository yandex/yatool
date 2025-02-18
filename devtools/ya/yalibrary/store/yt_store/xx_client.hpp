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
    YtStore(const char* yt_proxy, const char* yt_dir, const char* yt_token);
    ~YtStore();
    void DoTryRestore(const YtStoreClientRequest& req, YtStoreClientResponse& rsp);
    void PrepareData(const YtStorePrepareDataRequest& req, YtStorePrepareDataResponse& rsp);
    NYT::IClientPtr Client;
    std::string YtDir;
};

// vim:ts=4:sw=4:et:
