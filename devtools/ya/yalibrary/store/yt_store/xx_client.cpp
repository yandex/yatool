#include "xx_client.hpp"
#include "util/stream/printf.h"
#include <contrib/libs/libarchive/libarchive/archive.h>
#include <contrib/libs/libarchive/libarchive/archive_entry.h>
#include <library/cpp/ucompress/reader.h>
#include <util/digest/city_streaming.h>
#include <util/folder/path.h>
#include <util/generic/scope.h>
#include <yt/cpp/mapreduce/interface/logging/logger.h>

using namespace NYT;

// see .pyx
extern void YaYtStoreLoggingHook(const char* msg);

struct LogBridge: public NYT::ILogger {
    static ELevel CutLevel_;
    virtual void Log(ELevel level, const ::TSourceLocation& sourceLocation, const char* format, va_list args) override {
        const auto level_string = [](ELevel level) -> const char* {
            switch (level) {
                case ILogger::FATAL:
                    return "FATAL";
                case ILogger::ERROR:
                    return "WARN";
                case ILogger::INFO:
                    return "INFO";
                case ILogger::DEBUG:
                    return "DEBUG";
            }
            Y_UNREACHABLE();
        };

        if (level <= CutLevel_) {
            TString msg;
            TStringOutput out(msg);
            Printf(out, "%s %s:%i ", level_string(level), sourceLocation.File.data(), sourceLocation.Line);
            Printf(out, format, args);
            YaYtStoreLoggingHook(msg.c_str());
        }
    }
};

LogBridge::ELevel LogBridge::CutLevel_ = ELevel::ERROR;

static void InitializeYt() {
    static bool already = false;
    if (!already) {
        NYT::JoblessInitialize();

        // intrusive ptr will try to dealloc pointer so we need to malloc
        NYT::SetLogger(new LogBridge());

        // Woudnt be here if logLevelStr invalid
        TryFromString(to_lower(TConfig::Get()->LogLevel), LogBridge::CutLevel_);

        already = true;
    }
}

YtStore::YtStore(const char* yt_proxy, const char* yt_dir, const char* yt_token) {
    InitializeYt();
    if(yt_token && strlen(yt_token)) {
        this->Client = NYT::CreateClient(yt_proxy, NYT::TCreateClientOptions().Token(yt_token));
    } else {
        this->Client = NYT::CreateClient(yt_proxy);
    }
    this->YtDir = yt_dir;
}

YtStore::~YtStore() {
}

struct ChunkFetcher: public IWalkInput {
    YtStore& parent;
    const YtStoreClientRequest& req;
    int chunk_no;
    std::unordered_map<int, NYT::TNode> chunks;
    std::optional<TStreamingCityHash64> sch;

    void fetch(uint64_t chunk_i, uint64_t chunk_j = ~0) {
        TVector<NYT::TNode> keys;
        keys.push_back(NYT::TNode()("hash", req.Hash)("chunk_i", chunk_i));
        if (~chunk_j && chunk_j != chunk_i) {
            keys.push_back(NYT::TNode()("hash", req.Hash)("chunk_i", chunk_j));
        }
        auto rows = parent.Client->LookupRows(parent.YtDir + "/data", keys);
        for (auto& row : rows) {
            chunks[row.ChildAsUint64("chunk_i")] = std::move(row);
        }
        for (const auto& key : keys) {
            auto chunk = key.ChildAsUint64("chunk_i");
            if (!chunks.contains(chunk)) {
                ythrow yexception() << "Failed to fetch chunk #" << chunk << " for hash " << req.Hash;
            }
        }
    }

    ChunkFetcher(YtStore& parent, const YtStoreClientRequest& req)
        : parent(parent)
        , req(req)
        , chunk_no(0)
    {
        fetch(0, req.Chunks - 1);
        if (req.Chunks == 1) {
            auto real_hash = CityHash64(chunks[0].ChildAsString("data"));
            if (stoull(std::string(req.Hash)) != real_hash) {
                ythrow yexception() << "Hash mismatch: " << req.Hash << " != " << real_hash;
            }
        } else {
            const auto& last = chunks[req.Chunks - 1].ChildAsString("data");
            if (last.size() >= 64) {
                sch.emplace(
                    req.DataSize,
                    chunks[0].ChildAsString("data").c_str(),
                    last.c_str() + last.size() - 64);
            } else {
                if (req.Chunks > 2) {
                    fetch(req.Chunks - 2);
                }
                const auto& pre_last = chunks[req.Chunks - 2].ChildAsString("data");
                char tail[64];
                memcpy(tail, pre_last.c_str() + pre_last.size() - 64 + last.size(), 64 - last.size());
                memcpy(tail + 64 - last.size(), last.c_str(), last.size());
                sch.emplace(
                    req.DataSize,
                    chunks[0].ChildAsString("data").c_str(),
                    tail);
            }
        }
    }
    size_t DoUnboundedNext(const void** ptr) override {
        if (chunk_no == req.Chunks) {
            *ptr = nullptr;
            return 0;
        } else {
            chunks.erase(chunk_no - 1);
            const auto& cur = chunks[chunk_no].ChildAsString("data");
            if (sch.has_value()) {
                sch->Process(cur.c_str(), cur.size());
            }
            if (++chunk_no == req.Chunks) {
                if (sch.has_value()) {
                    auto real_hash = (*sch)();
                    if (stoull(std::string(req.Hash)) != real_hash) {
                        ythrow yexception() << "Hash mismatch: " << req.Hash << " != " << real_hash;
                    }
                }
            } else if (!chunks.contains(chunk_no)) {
                fetch(chunk_no);
            }
            *ptr = cur.c_str();
            return cur.size();
        }
    }
};

struct YtStoreGetter {
    YtStore& parent;
    const YtStoreClientRequest& req;
    ChunkFetcher fetcher;
    std::optional<NUCompress::TDecodedInput> decoder;
    TTempBuf buf;
    size_t decoded_size;

    YtStoreGetter(YtStore& parent, const YtStoreClientRequest& req)
        : parent(parent)
        , req(req)
        , fetcher(parent, req)
        , decoded_size(0)
    {
        if (req.Codec != nullptr && strcmp(req.Codec, "")) {
            decoder.emplace(&fetcher);
            buf = TTempBuf(64 << 10);
        }
    }

    la_ssize_t read(const void** buffer) {
        if (Y_LIKELY(decoder.has_value())) {
            *buffer = buf.Data();
            auto len_decoded = decoder->Read(buf.Data(), buf.Size());
            decoded_size += len_decoded;
            return len_decoded;
        }
        return fetcher.DoUnboundedNext(buffer);
    }

    static la_ssize_t archive_read_callback(struct archive* a, void* client_data, const void** buffer) {
        Y_UNUSED(a);
        return reinterpret_cast<YtStoreGetter*>(client_data)->read(buffer);
    }
};

void YtStore::DoTryRestore(const YtStoreClientRequest& req, YtStoreClientResponse& rsp) {
    struct archive* a = archive_read_new();
    struct archive* writer = archive_write_disk_new();
    struct archive_entry* entry = nullptr;
    const void* buf;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
    Y_DEFER {
        archive_read_free(a);
        archive_write_free(writer);
    };
    try {
        YtStoreGetter getter(*this, req);
        TFsPath root_dir = req.IntoDir;
        if (archive_read_open(a, &getter, nullptr, YtStoreGetter::archive_read_callback, nullptr) != ARCHIVE_OK) {
            ythrow yexception() << "Failed to open tar: " << archive_error_string(a);
        }
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            size_t size;
            la_int64_t offset;
            int r;
            archive_entry_set_pathname(entry, (root_dir / archive_entry_pathname(entry)).c_str());
            if(const char *src = archive_entry_hardlink(entry); src) {
                archive_entry_set_hardlink(entry, (root_dir / src).c_str());
            }
            if (archive_write_header(writer, entry) != ARCHIVE_OK) {
                ythrow yexception() << "Failed to extract tar: " << archive_error_string(writer);
            }
            while ((r = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK) {
                if (archive_write_data_block(writer, buf, size, offset) != ARCHIVE_OK) {
                    ythrow yexception() << "Failed to extract tar: " << archive_error_string(writer);
                }
            }
            if (r != ARCHIVE_EOF) {
                ythrow yexception() << "Failed to read archive: " << archive_error_string(a);
            }
            if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
                ythrow yexception() << "Failed to extract tar: " << archive_error_string(writer);
            }
        }
        // Ensure we readed all from cache (and checked hash thus)
        // Possibly will never happen IRL but useful for testing
        while (Y_UNLIKELY(getter.fetcher.DoUnboundedNext(&buf) != 0)) {
        };
        rsp.Success = true;
        rsp.DecodedSize = getter.decoded_size;
    } catch (yexception exc) {
        rsp.Success = false;
        snprintf(rsp.ErrorMsg, sizeof(rsp.ErrorMsg), "%s", exc.what());
    }
}

// vim:ts=4:sw=4:et:
