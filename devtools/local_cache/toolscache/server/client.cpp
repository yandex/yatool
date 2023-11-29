#include "server.h"

#include "devtools/local_cache/toolscache/service/service.h"
#include "devtools/local_cache/ac/service/service.h"
#include "devtools/local_cache/psingleton/service/service.h"
#include "devtools/local_cache/psingleton/systemptr.h"

#include <library/cpp/logger/stream.h>
#include <library/cpp/logger/null.h>
#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/singleton.h>
#include <util/folder/path.h>
#include <util/system/maxlen.h>

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

using namespace grpc;

namespace NToolsCache {
    static std::shared_ptr<Channel> GetClientChannel(const TSystemWideName& name, bool setRecvLimit = true) {
        ChannelArguments args;
        args.SetMaxSendMessageSize(2 * MAX_PATH);
        if (setRecvLimit) {
            args.SetMaxReceiveMessageSize(128);
        }

        ResourceQuota quota("memory_bound");
        args.SetResourceQuota(quota.Resize(1024 * 1024).SetMaxThreads(1));

        return CreateCustomChannel(name.ToGrpcAddress(), InsecureChannelCredentials(), args);
    }

    static void ForceGC(const TClientOptions& opts, const TSystemWideName& name, TLog& log) {
        auto channel = GetClientChannel(name);

        if (!opts.ForceTCGC.Empty()) {
            auto stats = NToolsCache::TToolsCacheClient(channel, opts.Deadline).ForceGC(opts.ForceTCGC.GetRef());

            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << "Current tools cache stats: " << stats << " , requested size: " << opts.ForceTCGC.GetRef() << Endl;
        }

        if (!opts.ForceACGC.Empty()) {
            auto stats = NACCache::TACCacheClient(channel, opts.Deadline).ForceGC(opts.ForceACGC.GetRef());

            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << "Current build cache stats: " << stats << " , requested size: " << opts.ForceACGC.GetRef() << Endl;
        }
    }

    static void LockUnlockResource(const TClientOptions& opts, const TSystemWideName& name, TLog& log, bool lockAction) {
        auto channel = GetClientChannel(name);

        TSBResource resource;
        TFsPath fsPath(lockAction ? opts.LockResource.GetRef() : opts.UnlockSBResource.GetRef());
        if (fsPath.IsAbsolute()) {
            resource.SetPath(fsPath.Dirname());
        } else if (ToString(fsPath.Basename()) == ToString(fsPath)) {
            resource.SetPath(GetToolDir());
        } else {
            ythrow yexception() << "Incorrect '" << fsPath << "' parameter, should be absolute or simple name (relative to tool_dir)";
        }
        resource.SetSBId(fsPath.Basename());

        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << (lockAction ? "Lock" : "Unlock") << " resource: " << resource << Endl;

        auto stats = lockAction ? NToolsCache::TToolsCacheClient(channel, opts.Deadline).LockResource(resource) : NToolsCache::TToolsCacheClient(channel, opts.Deadline).UnlockSBResource(resource);

        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << "Current stats: " << stats << Endl;
    }

    static void UnlockAllResources(const TClientOptions& opts, const TSystemWideName& name, TLog& log) {
        auto channel = GetClientChannel(name);

        auto stats = NToolsCache::TToolsCacheClient(channel, opts.Deadline).UnlockAllResources();

        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << "Current stats: " << stats << Endl;
    }

    static int Suspend(const TClientOptions& opts, const TSystemWideName& name, TLog& log) {
        auto channel = GetClientChannel(name);

        auto s = NUserService::TClient(channel).StopProcessing("TToolsCacheClient", opts.Deadline);
        if (s != NUserService::Suspended) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Unexpected state after Suspend call: " << (int)s << Endl;
            return -1;
        }
        return 0;
    }

    static int Resume(const TClientOptions& opts, const TSystemWideName& name, TLog& log) {
        auto channel = GetClientChannel(name);

        auto s = NUserService::TClient(channel).StartProcessing("TToolsCacheClient", opts.Deadline);
        if (s != NUserService::Processing) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Unexpected state after Resume call: " << (int)s << Endl;
            return -1;
        }
        return 0;
    }

    static int SetMasterMode(bool master, const TClientOptions& opts, const TSystemWideName& name, TLog& log) {
        auto channel = GetClientChannel(name);

        auto s = NUserService::TClient(channel).SetMasterMode("TToolsCacheClient", master, opts.Deadline);
        if (s != NUserService::Processing) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Unexpected state after SetMasterMode call: " << (int)s << Endl;
            return -1;
        }
        return 0;
    }

    void NotifyOtherTC(const NConfig::TConfig& config, i64 deadline, TLog& log) {
        auto tcLockFile = NToolsCachePrivate::GetLockName(config);

        using TType = TPSingleton<TFileReadWriteLock<TSystemWideName>, TSystemWideName>;
        // Create separate instance
        THolder<TType> tcOther = MakeHolder<TType>(tcLockFile, log);
        auto name = tcOther->GetInstanceName(Blocking);
        if (name.Empty()) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Lock file '" << tcLockFile << "' does not specify alive ya-tc service." << Endl;
            ythrow NUserService::TGrpcException("Did not find ya-tc service", grpc::StatusCode::UNAVAILABLE) << ", Did not find ya-tc service";
        }
        auto channel = GetClientChannel(name.GetRef(), false);

        try {
            auto client = NToolsCache::TToolsCacheClient(channel, deadline);
            NToolsCache::TToolsCacheServer::InsertMyself(config, client);
        } catch (const yexception& ex) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Error notifying other TC: " << ex.what() << Endl;
            throw;
        }
    }

    int ClientCode(const TClientOptions& opts, const NConfig::TConfig& config, TLog& log) {
        auto lockFile = NToolsCacheServerPrivate::GetServerLockName(config);

        using TType = TPSingleton<TFileReadWriteLock<TSystemWideName>, TSystemWideName>;
        TType* singleton = Singleton<TType>(lockFile, log);
        auto name = singleton->GetInstanceName(Blocking);
        if (name.Empty()) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_ERR, "ERR[CLIENT]") << "Lock file '" << lockFile << "' does not specify alive service." << Endl;
            ythrow NUserService::TGrpcException("Did not find service in the common place", grpc::StatusCode::UNAVAILABLE) << ", Did not find service in the common place";
        }
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[CLIENT]") << "Client got singleton" << Endl;

        if (!opts.ForceTCGC.Empty() || !opts.ForceACGC.Empty()) {
            ForceGC(opts, name.GetRef(), log);
            return 0;
        }

        if (!opts.LockResource.Empty() || !opts.UnlockSBResource.Empty()) {
            // Lock takes precedence over Unlock.
            LockUnlockResource(opts, name.GetRef(), log, !opts.LockResource.Empty());
            return 0;
        }

        if (opts.UnlockAllResources) {
            UnlockAllResources(opts, name.GetRef(), log);
            return 0;
        }

        if (opts.Resume) {
            return Resume(opts, name.GetRef(), log);
        }

        if (opts.Suspend) {
            return Suspend(opts, name.GetRef(), log);
        }

        if (opts.Readonly) {
            return SetMasterMode(false, opts, name.GetRef(), log);
        }

        if (opts.Readwrite) {
            return SetMasterMode(true, opts, name.GetRef(), log);
        }
        return 0;
    }
}
