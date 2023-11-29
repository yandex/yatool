#pragma once

#include "config.h"

#include "devtools/local_cache/ac/proto/ac.grpc.pb.h"
#include "devtools/local_cache/ac/dbbei.h"
#include "devtools/local_cache/psingleton/service/service.h"

#include <devtools/local_cache/common/logger-utils/simple_stats.h>

#include <library/cpp/config/config.h>

#include <util/generic/ptr.h>
#include <util/system/rwlock.h>

#include <chrono>
#include <grpc/grpc.h>

namespace NACCache {
    class TACCacheClient : TNonCopyable {
    public:
        TACCacheClient(std::shared_ptr<grpc::ChannelInterface> channel, int deadline)
            : Stub_(TACCache::NewStub(channel).release())
            , Deadline_(deadline)
        {
        }

        TStatus ForceGC(i64 targetSize);

    private:
        grpc::ClientContext& setDeadline(grpc::ClientContext& ctx) {
            if (Deadline_ != INT_MAX && Deadline_ != 0) {
                ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(Deadline_));
            }
            return ctx;
        }

        THolder<TACCache::Stub> Stub_;
        int Deadline_;
    };

    class TACCacheServer final: public TACCache::Service, public NUserService::IService, TNonCopyable {
    public:
        TACCacheServer(const NConfig::TConfig&, TLog& log, const TCriticalErrorHandler& handler, const NUserService::IServer*);
        ~TACCacheServer();

        /// NUserService::IService @{
        void StartProcessing() override;
        void StopProcessing() override;
        bool IsQuiescent() const noexcept override;
        int PollingDelay() const noexcept override;
        void SetMasterMode(bool master) override;
        bool GetMasterMode() const noexcept override;
        /// @}

        /// TACCache::Service @{
        grpc::Status Put(grpc::ServerContext* context, const TPutUid* request, TACStatus* response) override;
        grpc::Status Get(grpc::ServerContext* context, const TGetUid* request, TACStatus* response) override;
        grpc::Status Remove(grpc::ServerContext* context, const TRemoveUid* request, TACStatus* response) override;
        grpc::Status Has(grpc::ServerContext* context, const THasUid* request, TACStatus* response) override;
        grpc::Status ForceGC(grpc::ServerContext* context, const TForceGC* request, TStatus* response) override;
        grpc::Status GetTaskStats(grpc::ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) override;
        grpc::Status PutDeps(grpc::ServerContext* context, const TNodeDependencies* request, TACStatus* response) override;

        grpc::Status ReleaseAll(grpc::ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) override;
        grpc::Status GetCacheStats(grpc::ServerContext* context, const TStatus* request, grpc::ServerWriter<TStatus>* writer) override;
        grpc::Status AnalyzeDU(grpc::ServerContext* context, const TStatus* request, TDiskUsageSummary* response) override;
        grpc::Status SynchronousGC(grpc::ServerContext* context, const TSyncronousGC* request, TStatus* response) override;
        /// @}

    private:
        std::string ErrorMessage(const char* method) const;
        grpc::Status SetStatus(TStringBuf method, grpc::ServerContext* context) const;

        void RemoveOnFailure(const THash& hash, const TSystemError& e, grpc::ServerContext* context, TACStatus* response);

        /// Returns config section related to this service.
        const NConfig::TDict& GetSection() const;

        TCriticalErrorHandler ErrorHandler_;
        const NUserService::IServer* Server_;

        /// Maintain order of initialization in Start().
        THolder<TACCacheDBBE> DBBE_;

        /// Mutex to guard set's and reset's of *Handler_'s.
        TRWMutex DBBEMutex_;
        NConfig::TConfig Config_;
        TLog& Log_;
        /// In milliseconds.
        int QuiescenceTime_;
        EPollersConf PollMode_;
        bool MasterMode_;
        bool GraphInfo_;
        bool ForeignKeys_;
        /// benchmarking mode, no DB operations at all
        bool NoDb_;
        /// benchmarking mode, no FS operations for blobs
        bool NoBlobIO_;
        /// recreate Db
        bool Recreate_;

        NCachesPrivate::TSimpleStats HasStats_;
        NCachesPrivate::TSimpleStats PutStats_;
        NCachesPrivate::TSimpleStats GetStats_;
        NCachesPrivate::TSimpleStats RemoveStats_;
        NCachesPrivate::TSimpleStats PutDepsStats_;

        static TAtomic LastAccessTime_;
    };
}
