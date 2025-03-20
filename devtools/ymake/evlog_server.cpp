#include <devtools/ymake/evlog_server.h>
#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

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

    asio::awaitable<void> TServer::ForeignPlatformTargetEventHandler(const TString& line) {
        auto targetEvent = TryParseEventAs<NEvent::TForeignPlatformTarget>(line);
        if (targetEvent == nullptr) {
            co_return;
        }

        if (targetEvent->GetReachable()) {
            YDebug() << "EvlogServer: Reachable target found " << targetEvent->GetDir() << ", platform " << TForeignPlatformTarget_EPlatform_Name(targetEvent->GetPlatform()) << Endl;
            Conf_.AddTarget(targetEvent->GetDir());
            if (Mode_.Defined()) {
                if (Mode_ == EMode::Configure) {
                    co_await Configurator_.AddStartTarget(Exec_, targetEvent->GetDir(), targetEvent->GetModuleTag(), false);
                }
            } else {
                ReachableTargets_.insert({targetEvent->GetDir(), targetEvent->GetModuleTag(), false});
            }
        } else {
            YDebug() << "EvlogServer: Possible target found " << targetEvent->GetDir() << ", platform " << TForeignPlatformTarget_EPlatform_Name(targetEvent->GetPlatform()) << Endl;
            if (Mode_.Defined()) {
                if (Mode_ == EMode::Configure) {
                    co_await Configurator_.AddTarget(Exec_, targetEvent->GetDir());
                }
            } else {
                PossibleTargets_.insert(targetEvent->GetDir());
            }
        }

        co_return;
    }

    void TServer::AllForeignPlatformsReportedEventHandler(const TString&) {
        if (!Mode_.Defined()) {
            YDebug() << "EvlogServer: bypass state is unknown, continue in regular mode" << Endl;
            Conf_.ReadStartTargetsFromEvlog = false;
        } else if (Mode_ == EMode::CollectOnly) {
            YDebug() << "EvlogServer: external bypass is enabled, continue in regular mode" << Endl;
            Conf_.ReadStartTargetsFromEvlog = false;
        }
        YDebug() << "EvlogServer: DONE" << Endl;
    }

    asio::awaitable<void> TServer::BypassConfigureEventHandler(const TString& line) {
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
            for (auto& [dir, tag, followRecurses] : ReachableTargets_) {
                co_await Configurator_.AddStartTarget(Exec_, dir, tag, followRecurses);
            }
            for (auto dir : PossibleTargets_) {
                co_await Configurator_.AddTarget(Exec_, dir);
            }
            // We cannot use bypass from now since some targets are configured
            Conf_.DisableGrandBypass();
        }
        co_return;
    }

    asio::awaitable<void> TServer::ProcessStreamBlocking(IInputStream& input) {
        TString line;
        NJson::TJsonValue json;
        TString evtype;
        while (input.ReadLine(line)) {
            if (!NJson::ReadJsonTree(line, &json, false))
                continue;
            if (!NJson::GetString(json, "_typename", &evtype)) {
                continue;
            }

            if (evtype == "NEvent.TAllForeignPlatformsReported") {
                AllForeignPlatformsReportedEventHandler(line);
                break;
            }

            if (evtype == "NEvent.TBypassConfigure") {
                co_await BypassConfigureEventHandler(line);
            }

            if (evtype == "NEvent.TForeignPlatformTarget") {
                co_await ForeignPlatformTargetEventHandler(line);
            }
        }
        co_return;
    }
} // namespace NEvlogServer
