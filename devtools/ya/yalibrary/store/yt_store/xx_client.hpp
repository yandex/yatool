#pragma once

#include <yt/cpp/mapreduce/interface/client.h>

struct YtStoreClientResponse {
    bool Success;
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

struct YtStore {
    YtStore(const char* yt_proxy, const char* yt_dir, const char* yt_token);
    ~YtStore();
    void DoTryRestore(const YtStoreClientRequest& req, YtStoreClientResponse& rsp);
    NYT::IClientPtr Client;
    std::string YtDir;
};

// vim:ts=4:sw=4:et:
