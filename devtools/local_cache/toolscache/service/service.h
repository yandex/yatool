#pragma once

#include "config.h"

#include "devtools/local_cache/toolscache/proto/tools.grpc.pb.h"
#include "devtools/local_cache/toolscache/dbbei.h"
#include "devtools/local_cache/psingleton/service/service.h"

#include <library/cpp/config/config.h>

#include <util/generic/ptr.h>
#include <util/system/rwlock.h>

#include <chrono>
#include <grpc/grpc.h>

namespace NToolsCache {
    class TToolsCacheClient : TNonCopyable {
    public:
        TToolsCacheClient(std::shared_ptr<grpc::ChannelInterface> channel, int deadline)
            : Stub_(TToolsCache::NewStub(channel).release())
            , Deadline_(deadline)
        {
        }

        TStatus ForceGC(i64 targetSize);
        TStatus LockResource(const TSBResource&);
        TStatus UnlockSBResource(const TSBResource&);
        TStatus UnlockAllResources();
        TServiceResponse NotifyNewService(const TServiceStarted&);

    private:
        grpc::ClientContext& setDeadline(grpc::ClientContext& ctx) {
            if (Deadline_ != INT_MAX && Deadline_ != 0) {
                ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(Deadline_));
            }
            return ctx;
        }

        THolder<TToolsCache::Stub> Stub_;
        int Deadline_;
    };

    class TToolsCacheServer final: public TToolsCache::Service, public NUserService::IService, TNonCopyable {
    public:
        TToolsCacheServer(const NConfig::TConfig&, TLog& log, const TCriticalErrorHandler& handler, const NUserService::IServer*);
        ~TToolsCacheServer();

        /// NUserService::IService @{
        void StartProcessing() override;
        void StopProcessing() override;
        bool IsQuiescent() const noexcept override;
        int PollingDelay() const noexcept override;
        void SetMasterMode(bool master) override;
        bool GetMasterMode() const noexcept override;
        /// @}

        /// TToolsCache::Service @{
        grpc::Status Notify(grpc::ServerContext* context, const TResourceUsed* request, TStatus* response) override;
        grpc::Status NotifyNewService(grpc::ServerContext* context, const TServiceStarted* request, TServiceResponse* response) override;
        grpc::Status ForceGC(grpc::ServerContext* context, const TForceGC* request, TStatus* response) override;
        grpc::Status LockResource(grpc::ServerContext* context, const TSBResource* request, TStatus* response) override;
        grpc::Status UnlockSBResource(grpc::ServerContext* context, const TSBResource* request, TStatus* response) override;
        grpc::Status UnlockAllResources(grpc::ServerContext* context, const NUserService::TPeer* request, TStatus* response) override;
        grpc::Status GetTaskStats(grpc::ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) override;
        /// @}

        static void InsertMyself(const NConfig::TConfig& config, TToolsCacheClient& client) {
            InsertMyself(config, nullptr, &client);
        }

    private:
        /// Insert myself as running service.
        static void InsertMyself(const NConfig::TConfig& config, TToolsCacheDBBE* dbbe, TToolsCacheClient* client);
        /// Returns config section related to this service.
        static const NConfig::TDict& GetSection(const NConfig::TConfig& config);

        TCriticalErrorHandler ErrorHandler_;

        /// Maintain order of initialization in Start().
        THolder<IThreadPool> Pool_;
        THolder<TToolsCacheDBBE> DBBE_;

        /// Mutex to guard set's and reset's of *Handler_'s.
        TRWMutex DBBEMutex_;
        NConfig::TConfig Config_;
        TLog& Log_;
        /// In milliseconds.
        int QuiescenceTime_;
        EPollersConf PollMode_;
        bool MasterMode_;
        /// benchmarking mode
        bool NoDb_;

        static TAtomic LastAccessTime_;
    };
}
