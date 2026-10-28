#include "announcer.h"

#include <devtools/shared_cache/proto/telemetry.grpc.pb.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/folder/path.h>
#include <util/generic/guid.h>
#include <util/generic/yexception.h>
#include <util/random/random.h>
#include <util/string/builder.h>
#include <util/system/env.h>
#include <util/system/fstat.h>
#include <util/system/hostname.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace NACCache::NDistCache {
    namespace {
        using TClock = std::chrono::steady_clock;
        // Account protobuf memory as well as bytes; leave room for the envelope
        // under the receiver's 1 MiB message limit.
        constexpr size_t MaxBatchInputBytes = 1024 * 1024;
        constexpr size_t MaxRecordBytes = 256 * 1024;

        auto Milliseconds(TDuration duration) {
            return std::chrono::milliseconds(duration.MilliSeconds());
        }

        ESendResult Classify(const grpc::Status& status) {
            if (status.ok()) {
                return ESendResult::Success;
            }
            switch (status.error_code()) {
                case grpc::StatusCode::UNAVAILABLE:
                case grpc::StatusCode::DEADLINE_EXCEEDED:
                case grpc::StatusCode::RESOURCE_EXHAUSTED:
                case grpc::StatusCode::ABORTED:
                    return ESendResult::Retry;
                default:
                    return ESendResult::Rejected;
            }
        }

    } // namespace

    class TAnnouncer::TImpl {
    public:
        TImpl(TOptions options, TLog& log, TTransport transport)
            : Options_(std::move(options))
            , Log_(log)
            , Transport_(std::move(transport))
            , Session_(CreateGuidAsString())
        {
            while (Options_.Url.EndsWith('/')) {
                Options_.Url.pop_back();
            }
            const bool insecure = Options_.Url.StartsWith("grpc://");
            const bool secure = Options_.Url.StartsWith("grpcs://");
            Y_ENSURE(insecure || secure, "dist-cache URL must use grpc:// or grpcs://");
            auto authority = TStringBuf(Options_.Url).SubStr(insecure ? 7 : 8);
            Y_ENSURE(!authority.empty() && authority.find_first_of("/@\r\n\t ?#") == TStringBuf::npos,
                     "dist-cache URL must contain only a host and port");
            Y_ENSURE(Options_.BatchSize > 0 && Options_.BatchSize <= 500 && Options_.MaxQueueItems > 0 &&
                         Options_.MaxQueueBytes > 0 && Options_.MaxAttempts > 0 && Options_.MaxAttempts <= 10 &&
                         Options_.FlushInterval && Options_.RequestTimeout && Options_.Cooldown && Options_.DrainTimeout,
                     "invalid dist-cache limits");
            Worker_ = std::thread([this] { Run(); });
        }

        ~TImpl() {
            Stop();
        }

        const TString& Url() const noexcept {
            return Options_.Url;
        }

        void Enqueue(TPutUid&& request) noexcept {
            try {
                const size_t bytes = request.SpaceUsedLong() + sizeof(TItem);
                const bool valid = request.DBFileNamesSize() == 0 || request.DBFileNamesSize() == request.BlobInfoSize();
                std::lock_guard guard(Mutex_);
                if (Exited_ || Stopping_) {
                    return;
                }
                if (TClock::now() < CooldownUntil_) {
                    ++DroppedAnnouncementsTotal_;
                    return;
                }
                if (!valid || bytes > MaxRecordBytes || request.BlobInfoSize() > 8192) {
                    ++DroppedAnnouncementsTotal_;
                    return;
                } else if (bytes > Options_.MaxQueueBytes - OutstandingBytes_ ||
                           OutstandingItems_ >= Options_.MaxQueueItems) {
                    // Keep accepted observations; a local burst need not mean a failed receiver.
                    ++DroppedAnnouncementsTotal_;
                    QueueOverflowed_ = true;
                } else {
                    Queue_.emplace_back(std::move(request), bytes);
                    OutstandingBytes_ += bytes;
                    ++OutstandingItems_;
                }
                Wake_.notify_one();
            } catch (...) {
                // Telemetry failures must not change the cache RPC result.
                std::lock_guard guard(Mutex_);
                if (!Exited_) {
                    EnterCooldown();
                    Wake_.notify_one();
                }
            }
        }

        bool IsIdle() const noexcept {
            std::lock_guard guard(Mutex_);
            return Exited_ || (Queue_.empty() && !Working_ && !DiscardQueue_);
        }

        void Stop() noexcept {
            {
                std::lock_guard guard(Mutex_);
                if (!Stopping_) {
                    Stopping_ = true;
                    DrainDeadline_ = TClock::now() + Milliseconds(Options_.DrainTimeout);
                }
                Wake_.notify_one();
            }
            if (Worker_.joinable()) {
                Worker_.join();
            }
        }

    private:
        struct TItem {
            TPutUid Request;
            size_t Bytes;
            ui64 PutTimestampMs = TInstant::Now().MilliSeconds();
            TClock::time_point QueuedAt = TClock::now();

            TItem(TPutUid&& request, size_t bytes)
                : Request(std::move(request))
                , Bytes(bytes)
            {
            }
        };

        void Log(TStringBuf message) noexcept {
            try {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_WARNING, "WARN[DIST-CACHE]") << message << Endl;
            } catch (...) {
            }
        }

        // Caller holds Mutex_. Queue destruction is deferred to the worker.
        void EnterCooldown() {
            CooldownUntil_ = TClock::now() + Milliseconds(Options_.Cooldown);
            DiscardQueue_ = true;
        }

        sharedcache::v1::Producer Producer() const {
            sharedcache::v1::Producer producer;
            producer.set_host(HostName());
            producer.set_user(GetEnv("USER"));
            producer.set_session(Session_);
            return producer;
        }

        sharedcache::v1::AnnounceRequest Batch(const std::vector<TItem>& items, ui64 droppedAnnouncementsTotal) const {
            sharedcache::v1::AnnounceRequest batch;
            batch.set_version(1);
            batch.set_batch_id(CreateGuidAsString());
            *batch.mutable_producer() = Producer();
            batch.set_dropped_announcements_total(droppedAnnouncementsTotal);
            for (const auto& item : items) {
                const auto& request = item.Request;
                auto* record = batch.add_announcements();
                try {
                    Y_ENSURE(!request.GetACHash().GetUid().empty() && request.GetACHash().GetUid().size() <= 1024, "invalid UID");
                    record->set_uid(request.GetACHash().GetUid());
                    record->set_put_timestamp_ms(item.PutTimestampMs);
                    switch (request.GetDistCacheUidKind()) {
                        case TPutUid::ACTION:
                            record->set_uid_kind(sharedcache::v1::ACTION);
                            break;
                        case TPutUid::CONTENT:
                            record->set_uid_kind(sharedcache::v1::CONTENT);
                            break;
                        default:
                            record->set_uid_kind(sharedcache::v1::UNSPECIFIED);
                            break;
                    }
                    for (size_t i = 0; i != request.BlobInfoSize(); ++i) {
                        const auto& blob = request.GetBlobInfo(i);
                        auto* file = record->add_manifest();
                        file->set_path(request.DBFileNamesSize() ? request.GetDBFileNames(i)
                                                                 : TFsPath(blob.GetPath()).RelativeTo(request.GetRootPath()).GetPath());
                        // DBFileNames can bypass relative-path computation. Drop a bad
                        // observation here: receiver rejection would discard the whole batch.
                        Y_ENSURE(!file->path().empty() &&
                                     !file->path().StartsWith('/') && !file->path().StartsWith("../") &&
                                     file->path() != ".." && !file->path().Contains("/../") &&
                                     !file->path().Contains('\\'), "invalid relative path");
                        const auto& hash = blob.GetCASHash().GetUid();
                        Y_ENSURE(!hash.empty() && hash.size() <= 1024, "invalid CAS digest");
                        file->set_cas_hash(hash);
                        if (!Options_.CacheDir.empty() && hash.size() >= 2) {
                            const auto path = JoinFsPaths(Options_.CacheDir, hash.substr(0, 1), hash.substr(1, 1), hash);
                            const TFileStat stat(path, true);
                            if (!stat.IsNull()) {
                                file->set_size(stat.Size);
                                file->set_mode(stat.Mode);
                            }
                        }
                    }
                } catch (...) {
                    batch.mutable_announcements()->RemoveLast();
                }
            }
            return batch;
        }

        // Channel creation, TLS and all RPC work stay on the worker thread.
        void InitializeTransport() {
            if (Transport_.Announce) {
                return;
            }
            const bool secure = Options_.Url.StartsWith("grpcs://");
            auto credentials = secure ? grpc::SslCredentials(grpc::SslCredentialsOptions{}) : grpc::InsecureChannelCredentials();
            grpc::ChannelArguments arguments;
            arguments.SetMaxReceiveMessageSize(64 * 1024);
            arguments.SetInt(GRPC_ARG_ENABLE_RETRIES, 0);
            arguments.SetInt(GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS, 5000);
            auto channel = grpc::CreateCustomChannel(Options_.Url.substr(secure ? 8 : 7), credentials, arguments);
            Stub_ = sharedcache::v1::Telemetry::NewStub(channel);
            Transport_.Announce = [this](const sharedcache::v1::AnnounceRequest& request) {
                grpc::ClientContext context;
                context.set_deadline(std::chrono::system_clock::now() + Milliseconds(Options_.RequestTimeout));
                sharedcache::v1::Reply reply;
                return Classify(Stub_->Announce(&context, request, &reply));
            };
        }

        template <typename TSend>
        bool Deliver(TSend send) {
            for (size_t attempt = 0; attempt < Options_.MaxAttempts; ++attempt) {
                {
                    std::lock_guard lock(Mutex_);
                    if (Stopping_ && TClock::now() >= DrainDeadline_) {
                        return false;
                    }
                }
                ESendResult result = ESendResult::Retry;
                try {
                    result = send();
                } catch (...) {
                    Log("announcement transport failed");
                }
                if (result == ESendResult::Success) {
                    return true;
                }
                if (result == ESendResult::Rejected) {
                    Log("announcement rejected by receiver");
                    return false;
                }
                std::unique_lock lock(Mutex_);
                if (DiscardQueue_ || (Stopping_ && TClock::now() >= DrainDeadline_)) {
                    return false;
                }
                if (attempt + 1 < Options_.MaxAttempts) {
                    const ui64 base = Options_.RetryDelay.MilliSeconds() << attempt;
                    const auto next = TClock::now() + std::chrono::milliseconds(base + RandomNumber<ui64>(base / 2 + 1));
                    while (!DiscardQueue_ && TClock::now() < next) {
                        if (Stopping_ && TClock::now() >= DrainDeadline_) {
                            return false;
                        }
                        Wake_.wait_until(lock, Min(next, DrainDeadline_));
                    }
                }
            }
            return false;
        }

        void Run() noexcept {
            try {
                InitializeTransport();
                Loop();
            } catch (...) {
                Log("announcement worker failed; cache operations remain enabled");
                std::lock_guard guard(Mutex_);
                Queue_.clear();
                OutstandingBytes_ = OutstandingItems_ = 0;
                Working_ = DiscardQueue_ = false;
                Stopping_ = true;
                Exited_ = true;
            }
        }

        void Loop() {
            std::unique_lock lock(Mutex_);
            Working_ = false;
            TClock::time_point nextOverflowLog{};
            for (;;) {
                if (QueueOverflowed_) {
                    QueueOverflowed_ = false;
                    const auto now = TClock::now();
                    if (now >= nextOverflowLog) {
                        nextOverflowLog = now + std::chrono::minutes(1);
                        lock.unlock();
                        Log("announcement queue full; dropping new observations, keeping accepted ones");
                        lock.lock();
                    }
                }
                if (DiscardQueue_ || (Stopping_ && !Queue_.empty() && TClock::now() >= DrainDeadline_)) {
                    std::deque<TItem> discarded;
                    discarded.swap(Queue_);
                    DroppedAnnouncementsTotal_ += discarded.size();
                    for (const auto& item : discarded) {
                        OutstandingBytes_ -= item.Bytes;
                        --OutstandingItems_;
                    }
                    DiscardQueue_ = false;
                    Working_ = true;
                    lock.unlock();
                    Log(TStringBuilder() << "announcement queue discarded; entering cooldown; queued_observations=" << discarded.size());
                    discarded.clear();
                    lock.lock();
                    Working_ = false;
                }
                if (Stopping_ && Queue_.empty()) {
                    Exited_ = true;
                    return;
                }
                if (Queue_.empty()) {
                    Wake_.wait(lock);
                    continue;
                }
                const auto now = TClock::now();
                if (now < CooldownUntil_) {
                    Wake_.wait_until(lock, Min(CooldownUntil_, DrainDeadline_));
                    continue;
                }
                const auto flushAt = Queue_.front().QueuedAt + Milliseconds(Options_.FlushInterval);
                if (Queue_.size() < Options_.BatchSize && !Stopping_ && now < flushAt) {
                    Wake_.wait_until(lock, flushAt);
                    continue;
                }
                std::vector<TItem> items;
                size_t bytes = 0;
                size_t files = 0;
                while (!Queue_.empty() && items.size() < Options_.BatchSize &&
                       bytes + Queue_.front().Bytes <= MaxBatchInputBytes &&
                       files + Queue_.front().Request.BlobInfoSize() <= 8192) {
                    files += Queue_.front().Request.BlobInfoSize();
                    bytes += Queue_.front().Bytes;
                    items.emplace_back(std::move(Queue_.front()));
                    Queue_.pop_front();
                }
                Working_ = true;
                const auto droppedAnnouncementsTotal = DroppedAnnouncementsTotal_;
                lock.unlock();
                bool success = false;
                size_t dropped = 0;
                try {
                    auto batch = Batch(items, droppedAnnouncementsTotal);
                    dropped = items.size() - batch.announcements_size();
                    batch.set_dropped_announcements_total(droppedAnnouncementsTotal + dropped);
                    success = batch.announcements().empty() || Deliver([&] { return Transport_.Announce(batch); });
                } catch (...) {
                    Log("cannot serialize announcement batch");
                }
                lock.lock();
                DroppedAnnouncementsTotal_ += dropped;
                const auto count = items.size();
                if (!success) {
                    EnterCooldown();
                }
                // Release protobufs outside the producer's critical section.
                lock.unlock();
                if (!success) {
                    Log(TStringBuilder() << "announcement batch failed; batch_observations=" << count);
                }
                items.clear();
                lock.lock();
                OutstandingBytes_ -= bytes;
                OutstandingItems_ -= count;
                Working_ = false;
            }
        }

        TOptions Options_;
        TLog& Log_;
        TTransport Transport_;
        std::unique_ptr<sharedcache::v1::Telemetry::Stub> Stub_;
        TString Session_;
        mutable std::mutex Mutex_;
        std::condition_variable Wake_;
        std::deque<TItem> Queue_;
        size_t OutstandingItems_ = 0;
        ui64 DroppedAnnouncementsTotal_ = 0;
        size_t OutstandingBytes_ = 0;
        bool Working_ = true;
        bool DiscardQueue_ = false;
        bool QueueOverflowed_ = false;
        bool Stopping_ = false;
        bool Exited_ = false;
        TClock::time_point CooldownUntil_{};
        TClock::time_point DrainDeadline_ = TClock::time_point::max();
        std::thread Worker_;
    };

    TAnnouncer::TAnnouncer(TOptions options, TLog& log, TTransport transport)
        : Impl_(MakeHolder<TImpl>(std::move(options), log, std::move(transport)))
    {
    }

    TAnnouncer::~TAnnouncer() = default;

    void TAnnouncer::Enqueue(TPutUid&& request) noexcept {
        Impl_->Enqueue(std::move(request));
    }

    bool TAnnouncer::IsIdle() const noexcept {
        return Impl_->IsIdle();
    }
    void TAnnouncer::Stop() noexcept {
        Impl_->Stop();
    }
    const TString& TAnnouncer::Url() const noexcept {
        return Impl_->Url();
    }
} // namespace NACCache::NDistCache
