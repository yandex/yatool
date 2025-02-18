#include "xx_client.hpp"
#include "util/stream/printf.h"
#include <contrib/libs/libarchive/libarchive/archive.h>
#include <contrib/libs/libarchive/libarchive/archive_entry.h>
#include <library/cpp/blockcodecs/core/codecs.h>
#include <library/cpp/ucompress/reader.h>
#include <library/cpp/ucompress/writer.h>
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
    if (yt_token && strlen(yt_token)) {
        this->Client = NYT::CreateClient(yt_proxy, NYT::TCreateClientOptions().Token(yt_token));
    } else {
        this->Client = NYT::CreateClient(yt_proxy);
    }
    NYT::TConfig::Get()->ConnectTimeout = TDuration::Seconds(5);
    NYT::TConfig::Get()->SocketTimeout = TDuration::Seconds(5);
    NYT::TConfig::Get()->RetryInterval = TDuration::MilliSeconds(500);
    NYT::TConfig::Get()->RetryCount = 5;
    this->YtDir = yt_dir;
}

YtStore::~YtStore() {
}

struct ChunkFetcher: public IWalkInput {
    YtStore& Parent;
    const YtStoreClientRequest& Req;
    YtStoreClientResponse& Rsp;
    int ChunkNo;
    std::unordered_map<int, NYT::TNode> Chunks;
    std::optional<TStreamingCityHash64> Sch;

    void Fetch(uint64_t chunk_i, uint64_t chunk_j = ~0) {
        TVector<NYT::TNode> keys;
        keys.push_back(NYT::TNode()("hash", Req.Hash)("chunk_i", chunk_i));
        if (~chunk_j && chunk_j != chunk_i) {
            keys.push_back(NYT::TNode()("hash", Req.Hash)("chunk_i", chunk_j));
        }
        TNode::TListType rows;
        try {
            rows = Parent.Client->LookupRows(Parent.YtDir + "/data", keys, TLookupRowsOptions().Timeout(TDuration::Seconds(60)));
        } catch (std::exception& e) {
            Rsp.NetworkErrors = true;
            throw;
        }
        for (auto& row : rows) {
            Chunks[row.ChildAsUint64("chunk_i")] = std::move(row);
        }
        for (const auto& key : keys) {
            auto chunk = key.ChildAsUint64("chunk_i");
            if (!Chunks.contains(chunk)) {
                ythrow yexception() << "Failed to fetch chunk #" << chunk << " for hash " << Req.Hash;
            }
        }
    }

    ChunkFetcher(YtStore& parent, const YtStoreClientRequest& req, YtStoreClientResponse& rsp)
        : Parent(parent)
        , Req(req)
        , Rsp(rsp)
        , ChunkNo(0)
    {
        Fetch(0, req.Chunks - 1);
        if (req.Chunks == 1) {
            auto real_hash = CityHash64(Chunks[0].ChildAsString("data"));
            if (stoull(std::string(req.Hash)) != real_hash) {
                ythrow yexception() << "Hash mismatch: " << req.Hash << " != " << real_hash;
            }
        } else {
            const auto& last = Chunks[req.Chunks - 1].ChildAsString("data");
            if (last.size() >= 64) {
                Sch.emplace(
                    req.DataSize,
                    Chunks[0].ChildAsString("data").c_str(),
                    last.c_str() + last.size() - 64);
            } else {
                if (req.Chunks > 2) {
                    Fetch(req.Chunks - 2);
                }
                const auto& pre_last = Chunks[req.Chunks - 2].ChildAsString("data");
                char tail[64];
                memcpy(tail, pre_last.c_str() + pre_last.size() - 64 + last.size(), 64 - last.size());
                memcpy(tail + 64 - last.size(), last.c_str(), last.size());
                Sch.emplace(
                    req.DataSize,
                    Chunks[0].ChildAsString("data").c_str(),
                    tail);
            }
        }
    }
    size_t DoUnboundedNext(const void** ptr) override {
        if (ChunkNo == Req.Chunks) {
            *ptr = nullptr;
            return 0;
        } else {
            Chunks.erase(ChunkNo - 1);
            const auto& cur = Chunks[ChunkNo].ChildAsString("data");
            if (Sch.has_value()) {
                Sch->Process(cur.c_str(), cur.size());
            }
            if (++ChunkNo == Req.Chunks) {
                if (Sch.has_value()) {
                    auto real_hash = (*Sch)();
                    if (stoull(std::string(Req.Hash)) != real_hash) {
                        ythrow yexception() << "Hash mismatch: " << Req.Hash << " != " << real_hash;
                    }
                }
            } else if (!Chunks.contains(ChunkNo)) {
                Fetch(ChunkNo);
            }
            *ptr = cur.c_str();
            return cur.size();
        }
    }
};

struct YtStoreGetter {
    YtStore& Parent;
    const YtStoreClientRequest& Req;
    ChunkFetcher Fetcher;
    std::optional<NUCompress::TDecodedInput> Decoder;
    TTempBuf Buf;
    size_t DecodedSize;

    YtStoreGetter(YtStore& parent, const YtStoreClientRequest& req, YtStoreClientResponse& rsp)
        : Parent(parent)
        , Req(req)
        , Fetcher(parent, req, rsp)
        , DecodedSize(0)
    {
        if (req.Codec != nullptr && strcmp(req.Codec, "")) {
            Decoder.emplace(&Fetcher);
            Buf = TTempBuf(64 << 10);
        }
    }

    la_ssize_t Read(const void** buffer) {
        if (Y_LIKELY(Decoder.has_value())) {
            *buffer = Buf.Data();
            auto len_decoded = Decoder->Read(Buf.Data(), Buf.Size());
            DecodedSize += len_decoded;
            return len_decoded;
        }
        return Fetcher.DoUnboundedNext(buffer);
    }

    static la_ssize_t Callback(struct archive* a, void* client_data, const void** buffer) {
        Y_UNUSED(a);
        return reinterpret_cast<YtStoreGetter*>(client_data)->Read(buffer);
    }
};

struct ArchiveWriter {
    TFileOutput Output;
    std::optional<NUCompress::TCodedOutput> Encoder;
    size_t RawWritten;

    ArchiveWriter(const char *path, const char *codec): Output(path), RawWritten(0) {
        if (codec != nullptr && strcmp(codec, "")) {
            Encoder.emplace(&Output, NBlockCodecs::Codec(codec));
        }
    }

    la_ssize_t Write(const void *buffer, size_t length) {
        RawWritten += length;
        if (Encoder.has_value()) {
            Encoder->Write(buffer, length);
        } else {
            Output.Write(buffer, length);
        }
        return length;
    }

    static la_ssize_t Callback(struct archive *a, void *client_data, const void *buffer, size_t length) {
        Y_UNUSED(a);
        return reinterpret_cast<ArchiveWriter*>(client_data)->Write(buffer, length);
    }
};

namespace {

static inline void Check(int code, struct archive *a, const char *msg) {
    if (code != ARCHIVE_OK) {
        ythrow yexception() << msg << ": " << archive_error_string(a);
    }
}

const char *MSG_CREATE = "Failed to create tar";
const char *MSG_OPEN = "Failed to open tar";
const char *MSG_EXTRACT = "Failed to extract tar";

}

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
    rsp.NetworkErrors = false;
    try {
        YtStoreGetter getter(*this, req, rsp);
        TFsPath root_dir = req.IntoDir;
        Check(archive_read_open(a, &getter, nullptr, YtStoreGetter::Callback, nullptr), a, MSG_OPEN);
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            size_t size;
            la_int64_t offset;
            int r;
            archive_entry_set_pathname(entry, (root_dir / archive_entry_pathname(entry)).c_str());
            if (const char* src = archive_entry_hardlink(entry); src) {
                archive_entry_set_hardlink(entry, (root_dir / src).c_str());
            }
            Check(archive_write_header(writer, entry), writer, MSG_EXTRACT);
            while ((r = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK) {
                Check(archive_write_data_block(writer, buf, size, offset), writer, MSG_EXTRACT);
            }
            if (r != ARCHIVE_EOF) {
                ythrow yexception() << "Failed to read archive: " << archive_error_string(a);
            }
            Check(archive_write_finish_entry(writer), writer, MSG_EXTRACT);
        }
        // Ensure we readed all from cache (and checked hash thus)
        // Possibly will never happen IRL but useful for testing
        while (Y_UNLIKELY(getter.Fetcher.DoUnboundedNext(&buf) != 0)) {
        };
        rsp.Success = true;
        rsp.DecodedSize = getter.DecodedSize;
    } catch (yexception exc) {
        rsp.Success = false;
        snprintf(rsp.ErrorMsg, sizeof(rsp.ErrorMsg), "%s", exc.what());
    }
}

void YtStore::PrepareData(const YtStorePrepareDataRequest& req, YtStorePrepareDataResponse& rsp) {
    try {
        struct archive* a = archive_write_new();
        archive_write_add_filter_none(a);
        archive_write_set_format_gnutar(a);
        archive_write_set_bytes_in_last_block(a, 1);
        ArchiveWriter writer(req.OutPath, req.Codec);
        Y_DEFER {
            archive_write_free(a);
        };
        Check(archive_write_open2(a, &writer, nullptr, ArchiveWriter::Callback, nullptr, nullptr), a, MSG_CREATE);
        TFsPath root_dir = req.RootDir;
        struct archive_entry* entry = nullptr;
        for(const auto &path: req.Files) {
            struct archive *disk = archive_read_disk_new();
            Y_DEFER {
                archive_read_free(disk);
            };
            Check(archive_read_disk_open(disk, path), disk, "Failed to read file");

            entry = archive_entry_new();
            Y_DEFER {
                archive_entry_free(entry);
            };
            Check(archive_read_next_header2(disk, entry), disk, "Failed to read file");

            archive_entry_set_pathname(entry, TFsPath(archive_entry_pathname(entry)).RelativePath(root_dir).c_str());
            archive_entry_set_mtime(entry, 0, 0);
            archive_entry_set_uid(entry, 0);
            archive_entry_set_gid(entry, 0);

            Check(archive_write_header(a, entry), a, "Failed to write tar");
            if (!archive_entry_hardlink(entry) && !archive_entry_symlink(entry)) {
                TMappedFileInput file_input(path);
                if (file_input.Avail()) {
                    auto write = archive_write_data(a, file_input.Buf(), file_input.Avail());
                    if (write == ARCHIVE_FATAL || write <=0) {
                        ythrow yexception() << "Failed to write tar: " << path << " " << write << archive_error_string(a);
                    }
                }
            }
        }
        rsp.RawSize = writer.RawWritten;
        rsp.Success = true;
    } catch (yexception exc) {
        rsp.Success = false;
        snprintf(rsp.ErrorMsg, sizeof(rsp.ErrorMsg), "%s", exc.what());
    }
}

// vim:ts=4:sw=4:et:
