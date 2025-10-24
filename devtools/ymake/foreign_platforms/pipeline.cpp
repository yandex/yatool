#include <asio/awaitable.hpp>
#include <library/cpp/json/json_reader.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <util/generic/queue.h>

#include <devtools/ymake/conf.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/foreign_platforms/pipeline.h>

namespace NForeignTargetPipeline {

// External pipeline

class TTraceLineWriter : public TLineWriter {
public:
    void WriteLine(const TString& target) override {
        FORCE_TRACE(T, target);
    }
    void WriteLineUniq(const TFileView& fileview, const TString& target) override {
        FORCE_UNIQ_CONFIGURE_TRACE(fileview, T, target);
    }
    void WriteBypassLine(const TString& target) override {
        FORCE_TRACE(C, target);
    }
};


THolder<TLineReader> TForeignTargetPipelineExternal::CreateReader(TBuildConfiguration& conf) {
    return MakeHolder<TStreamLineReader>(*conf.InputStream);
}
THolder<TLineWriter> TForeignTargetPipelineExternal::CreateWriter(TBuildConfiguration&){
    return MakeHolder<TTraceLineWriter>();
}
bool TForeignTargetPipelineExternal::RegisterConfig(const TVector<const char*>&) {
    return true;
}

// Internal pipeline

class TQueueLineReader : public TLineReader {
public:
    explicit TQueueLineReader(TAtomicSharedPtr<Queue> queue, size_t subscriptions)
        : Queue_(queue)
        , BypassesRemaining_(subscriptions)
        , FinalsRemaining_(subscriptions)
    {}

    asio::awaitable<std::optional<TString>> ReadLine() override {
        TString line;
        while (true) {
            line = co_await Queue_->async_receive();
            NJson::TJsonValue json;
            TString evtype;

            NJson::ReadJsonTree(line, &json, false);
            NJson::GetString(json, "_typename", &evtype);

            if (FinalsRemaining_ > 0 && evtype == "NEvent.TAllForeignPlatformsReported") {
                if (--FinalsRemaining_ == 0) {
                    break;
                }
                continue;
            }

            if (BypassesRemaining_ > 0 && evtype == "NEvent.TBypassConfigure") {
                bool enabled;
                NJson::GetBoolean(json, "Enabled", &enabled);
                if (enabled) {
                    if (--BypassesRemaining_ == 0) {
                        break;
                    }
                } else {
                    BypassesRemaining_ = 0;
                    break;
                }
                continue;
            }
            if (evtype == "NEvent.TForeignPlatformTarget") {
                break;
            }
        }
        co_return std::make_optional(line);
    }

private:
    TAtomicSharedPtr<Queue> Queue_;
    size_t BypassesRemaining_;
    size_t FinalsRemaining_;
};

class TQueueLineWriter : public TLineWriter {
public:
    explicit TQueueLineWriter(THashMap<ETransition, TAtomicSharedPtr<Queue>>& dests)
         : Destinations_(dests)
    {}

    ~TQueueLineWriter() {
        WriteLineInt(NYMake::EventToStr(NEvent::TAllForeignPlatformsReported{}));
    }

    void WriteLine(const TString& target) override {
        // This double-tracing is needed for some tests
        // and pool loops waiting for TAllForeignPlatformsReported.
        // TODO: get rid of it
        FORCE_TRACE(T, target);
        WriteLineInt(target);
    }
    void WriteLineUniq(const TFileView& fileView, const TString& target) override {
        // This double-tracing is needed for some tests
        // and pool loops waiting for TAllForeignPlatformsReported.
        // TODO: get rid of it
        FORCE_UNIQ_CONFIGURE_TRACE(fileView, T, target);
        if (LinesWritten_.insert({fileView.GetElemId(), ETraceEvent::T}).second) {
            WriteLineInt(target);
        }
    }
    void WriteBypassLine(const TString& target) override {
        // This double-tracing is needed for some tests
        // and pool loops waiting for TAllForeignPlatformsReported.
        // TODO: get rid of it
        FORCE_TRACE(C, target);
        WriteLineInt(target);
    }
private:

    constexpr ETransition TransitionFromPlatform(NEvent::TForeignPlatformTarget_EPlatform platform) {
        switch (platform) {
            case NEvent::TForeignPlatformTarget_EPlatform::TForeignPlatformTarget_EPlatform_TOOL:
                return ETransition::Tool;
            case NEvent::TForeignPlatformTarget_EPlatform::TForeignPlatformTarget_EPlatform_PIC:
                return ETransition::Pic;
            case NEvent::TForeignPlatformTarget_EPlatform::TForeignPlatformTarget_EPlatform_NOPIC:
                return ETransition::NoPic;
            default:
                return ETransition::None;
        }
    }

    void WriteLineInt(const TString& target) {
        NJson::TJsonValue json;
        TString evtype;

        if (!NJson::ReadJsonTree(target, &json, false)) {
            return;
        }
        if (!NJson::GetString(json, "_typename", &evtype)) {
            return;
        }

        asio::error_code ec;
        if (evtype == "NEvent.TAllForeignPlatformsReported") {
            for (auto& [_, dst] : Destinations_) {
                dst->try_send(ec, target);
            }
            return;
        }

        if (evtype == "NEvent.TBypassConfigure") {
            for (auto& [_, dst] : Destinations_) {
                dst->try_send(ec, target);
            }
            return;
        }

        if (evtype == "NEvent.TForeignPlatformTarget") {
            long long platform;
            if (!NJson::GetInteger(json, "Platform", &platform)) {
                return;
            }
            auto destinationKey = TransitionFromPlatform(static_cast<NEvent::TForeignPlatformTarget_EPlatform>(platform));
            if (destinationKey == ETransition::None) {
                return;
            }
            if (!Destinations_.contains(destinationKey)) {
                YDebug() << "Internal servermode: cannot find destination for event " << target << Endl;
                return;
            }
            Destinations_.at(destinationKey)->try_send(ec, target);
            return;
        }
    }

    THashMap<ETransition, TAtomicSharedPtr<Queue>> Destinations_;
    THashSet<std::pair<ui32, ETraceEvent>> LinesWritten_;
};

class TDummyLineWriter : public TLineWriter {
public:
    void WriteLine(const TString&) override {}
    void WriteLineUniq(const TFileView&, const TString&) override {}
    void WriteBypassLine(const TString&) override {}
};

THolder<TLineReader> TForeignTargetPipelineInternal::CreateReader(TBuildConfiguration& conf) {
    if (Subscriptions_.count(std::make_pair(conf.TargetPlatformId, conf.TransitionSource)) == 0) {
        return nullptr;
    }
    const auto& queue = PipesByTargetPlatformId_.at(conf.TargetPlatformId).at(conf.TransitionSource);
    return MakeHolder<TQueueLineReader>(queue, Subscriptions_.at(std::make_pair(conf.TargetPlatformId, conf.TransitionSource)));
}
THolder<TLineWriter> TForeignTargetPipelineInternal::CreateWriter(TBuildConfiguration& conf){
    if (Subscribers_.count(std::make_pair(conf.TargetPlatformId, conf.TransitionSource)) == 0) {
        return MakeHolder<TDummyLineWriter>();
    }
    const auto& queuesForPlatform = PipesByTargetPlatformId_.at(conf.TargetPlatformId);
    THashMap<ETransition, TAtomicSharedPtr<Queue>> dests;
    const auto& subscribers = Subscribers_.at(std::make_pair(conf.TargetPlatformId, conf.TransitionSource));
    for (const auto subscriber : subscribers) {
        dests[subscriber] = queuesForPlatform.at(subscriber);
    }
    return MakeHolder<TQueueLineWriter>(dests);
}
bool TForeignTargetPipelineInternal::RegisterConfig(const TVector<const char*>& config) {
    // TStartupOptions alone doesn't work somehow.
    // When replacing TBuildConfiguration parsing here with smth else,
    // make sure that errors during main TBuildConfiguration parsing
    // don't lead to hanging readers.
    TBuildConfiguration conf;
    NLastGetopt::TOpts opts;
    opts.ArgPermutation_ = NLastGetopt::REQUIRE_ORDER;
    conf.AddOptions(opts);
    const NLastGetopt::TOptsParseResult res(&opts, config.size(), const_cast<const char**>(config.data()));

    auto queue = MakeAtomicShared<Queue>(Executor_, 4096u);
    PipesByTargetPlatformId_[conf.TargetPlatformId][conf.TransitionSource] = queue;
    switch (conf.TransitionSource) {
        case ETransition::NoPic:
            Subscribers_[std::make_pair(conf.TargetPlatformId, conf.TransitionSource)].push_back(ETransition::Tool);
            Subscriptions_[std::make_pair(conf.TargetPlatformId, ETransition::Tool)]++;
            break;
        case ETransition::Pic:
            if (PipesByTargetPlatformId_[conf.TargetPlatformId].contains(ETransition::NoPic)) {
                Subscribers_[std::make_pair(conf.TargetPlatformId, ETransition::NoPic)].push_back(conf.TransitionSource);
                Subscriptions_[std::make_pair(conf.TargetPlatformId, conf.TransitionSource)]++;
            }
            Subscribers_[std::make_pair(conf.TargetPlatformId, conf.TransitionSource)].push_back(ETransition::Tool);
            Subscriptions_[std::make_pair(conf.TargetPlatformId, ETransition::Tool)]++;
            break;
        case ETransition::Tool:
        case ETransition::None:
            for (auto& [targetPlatform, platformQueues] : PipesByTargetPlatformId_) {
                platformQueues[ETransition::Tool] = queue;
                Subscriptions_[std::make_pair(conf.TargetPlatformId, conf.TransitionSource)] += Subscriptions_[std::make_pair(targetPlatform, ETransition::Tool)];
            }
            break;
    }

    return !conf.TargetPlatformId.empty() && conf.TransitionSource != ETransition::None;
}

} // namespace NForeignTargetPipeline
