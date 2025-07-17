#pragma once

#include <devtools/ymake/conf.h>
#include <devtools/ymake/foreign_platforms/io.h>
#include <library/cpp/containers/concurrent_hash/concurrent_hash.h>

#include <asio/experimental/concurrent_channel.hpp>

namespace NForeignTargetPipeline {

class TForeignTargetPipeline {
public:
    virtual THolder<TLineReader> CreateReader(TBuildConfiguration&) = 0;
    virtual THolder<TLineWriter> CreateWriter(TBuildConfiguration&) = 0;
    virtual bool RegisterConfig(const TVector<const char*>& config) = 0;
    virtual ~TForeignTargetPipeline() noexcept = default;
};

class TForeignTargetPipelineExternal : public TForeignTargetPipeline {
public:
    THolder<TLineReader> CreateReader(TBuildConfiguration& conf) override;
    THolder<TLineWriter> CreateWriter(TBuildConfiguration&) override;
    bool RegisterConfig(const TVector<const char*>& config) override;
};

using Queue = asio::experimental::concurrent_channel<void(asio::error_code, TString)>;

class TForeignTargetPipelineInternal : public TForeignTargetPipeline {
public:
    TForeignTargetPipelineInternal(asio::any_io_executor executor)
        : Executor_(executor)
    {
    }
    THolder<TLineReader> CreateReader(TBuildConfiguration& conf) override;
    THolder<TLineWriter> CreateWriter(TBuildConfiguration&) override;
    bool RegisterConfig(const TVector<const char*>& config) override;
private:
    // shared ptr to mitigate iterator invalidation
    THashMap<TString, THashMap<ETransition, TAtomicSharedPtr<Queue>>> PipesByTargetPlatformId_;
    THashMap<std::pair<TString, ETransition>, TVector<ETransition>> Subscribers_;
    THashMap<std::pair<TString, ETransition>, size_t> Subscriptions_;
    asio::any_io_executor Executor_;
};

} // namespace NForeignTargetPipeline
