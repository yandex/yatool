#include <devtools/ymake/evlog_server.h>
#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>

namespace NEvlogServer {

    template <class T, class = TGuardConversion<::google::protobuf::Message, T>>
    THolder<T> TryParseEventAs(const TString& str) {
        THolder<T> event;
        try {
            event = MakeHolder<T>(NProtobufJson::Json2Proto<T>(str));
        } catch (yexception) {
            return nullptr;
        }
        if (event->IsInitialized()) {
            return event;
        }
        return nullptr;
    }

    EHandlerResult TServer::ForeignPlatformTargetEventHandler(const TString& line) {
        auto targetEvent = TryParseEventAs<NEvent::TForeignPlatformTarget>(line);
        if (targetEvent == nullptr) {
            return EHandlerResult::Continue;
        }
        if (targetEvent->GetPlatform() != NEvent::TForeignPlatformTarget_EPlatform_TOOL) {
            return EHandlerResult::Continue;
        }

        if (targetEvent->GetReachable()) {
            YDebug() << "EvlogServer: Reachable target found " << targetEvent->GetDir() << Endl;
            Conf_.AddTarget(targetEvent->GetDir());
            if (Mode_.Defined()) {
                if (Mode_ == EMode::Configure) {
                    Configurator_.AddStartTarget(targetEvent->GetDir());
                }
            } else {
                ReachableTargets_.push_back(targetEvent->GetDir());
            }
        } else {
            YDebug() << "EvlogServer: Possible target found " << targetEvent->GetDir() << Endl;
            if (Mode_.Defined()) {
                if (Mode_ == EMode::Configure) {
                    Configurator_.AddTarget(targetEvent->GetDir());
                }
            } else {
                PossibleTargets_.push_back(targetEvent->GetDir());
            }
        }

        return EHandlerResult::Continue;
    }

    EHandlerResult TServer::AllForeignPlatformsReportedEventHandler(const TString&) {
        if (!Mode_.Defined()) {
            YDebug() << "EvlogServer: bypass state is unknown, continue in regular mode" << Endl;
            Conf_.ReadStartTargetsFromEvlog = false;
        } else if (Mode_ == EMode::CollectOnly) {
            YDebug() << "EvlogServer: external bypass is enabled, continue in regular mode" << Endl;
            Conf_.ReadStartTargetsFromEvlog = false;
        }
        YDebug() << "EvlogServer: DONE" << Endl;
        return EHandlerResult::AllDone;
    }

    EHandlerResult TServer::BypassConfigureEventHandler(const TString& line) {
        if (Mode_.Defined()) {
            throw TConfigurationError() << "EvlogServer: Received multiple BypassConfigure events";
        }
        auto bypassEvent = TryParseEventAs<NEvent::TBypassConfigure>(line);
        YDebug() << "EvlogServer: got external bypass event: " << bypassEvent->GetEnabled() << Endl;
        if (bypassEvent->GetEnabled()) {
            // continue collecting targets
            Mode_ = EMode::CollectOnly;
        } else {
            // configure all memoized targets and continue configuring on the fly
            Mode_ = EMode::Configure;
            for (auto dir : ReachableTargets_) {
                Configurator_.AddStartTarget(dir);
            }
            for (auto dir : PossibleTargets_) {
                Configurator_.AddTarget(dir);
            }
            // We cannot use bypass from now since some targets are configured
            Conf_.DisableGrandBypass();
        }
        return EHandlerResult::Continue;
    }

    void TServer::ProcessStreamBlocking(IInputStream& input) {
        TString line;
        NJson::TJsonValue json;
        TString evtype;
        while (input.ReadLine(line)) {
            if (!NJson::ReadJsonTree(line, &json, false))
                continue;
            if (!NJson::GetString(json, "_typename", &evtype)) {
                continue;
            }
            auto handlerIt = HandlerMap_.find(evtype);
            if (handlerIt == HandlerMap_.end()) {
                continue;
            }
            if (handlerIt->second(line) == EHandlerResult::AllDone) {
                break;
            }
        }
    };
} // namespace NEvlogServer
