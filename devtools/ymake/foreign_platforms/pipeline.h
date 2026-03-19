#pragma once

#include <devtools/ymake/conf.h>
#include <devtools/ymake/foreign_platforms/io.h>
#include <library/cpp/containers/concurrent_hash/concurrent_hash.h>

#include <asio/experimental/concurrent_channel.hpp>

namespace NForeignTargetPipeline {

class TForeignTargetPipeline {
public:
    virtual TLineReader* GetReader(TBuildConfiguration&) = 0;
    virtual TLineWriter* GetWriter(TBuildConfiguration&) = 0;
    virtual bool RegisterConfig(const TVector<const char*>& config) = 0;
    virtual void FinalizeConfig(TBuildConfiguration& conf) = 0;
    virtual ~TForeignTargetPipeline() noexcept = default;
};

class TForeignTargetPipelineExternal : public TForeignTargetPipeline {
public:
    TLineReader* GetReader(TBuildConfiguration& conf) override;
    TLineWriter* GetWriter(TBuildConfiguration&) override;
    bool RegisterConfig(const TVector<const char*>& config) override;
    void FinalizeConfig(TBuildConfiguration& conf) override;
private:
    THashMap<IInputStream*, THolder<TStreamLineReader>> Readers_;
    THolder<TLineWriter> Writer_;
};

using Queue = asio::experimental::concurrent_channel<void(asio::error_code, TString)>;

class TForeignTargetPipelineInternal : public TForeignTargetPipeline {
public:
    TForeignTargetPipelineInternal(asio::any_io_executor executor)
        : Executor_(executor)
    {
    }
    TLineReader* GetReader(TBuildConfiguration& conf) override;
    TLineWriter* GetWriter(TBuildConfiguration&) override;
    bool RegisterConfig(const TVector<const char*>& config) override;
    void FinalizeConfig(TBuildConfiguration& conf) override;
private:
    using TConfigKey = std::pair<TString, ETransition>;
    // shared ptr to mitigate iterator invalidation
    THashMap<TString, THashMap<ETransition, TAtomicSharedPtr<Queue>>> PipesByTargetPlatformId_;
    THashMap<TConfigKey, TVector<ETransition>> Subscribers_;
    THashMap<TConfigKey, size_t> Subscriptions_;
    THashMap<TConfigKey, THolder<TLineWriter>> Writers_;
    THashMap<TConfigKey, THolder<TLineReader>> Readers_;
    asio::any_io_executor Executor_;
};

} // namespace NForeignTargetPipeline
