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

    EHandlerResult ForeignPlatformTargetEventHandler(const TString& line, THolder<TYMake>& yMake) {
        auto targetEvent = TryParseEventAs<NEvent::TForeignPlatformTarget>(line);
        if (targetEvent == nullptr) {
            return EHandlerResult::Continue;
        }
        if (targetEvent->GetPlatform() != NEvent::TForeignPlatformTarget_EPlatform_TOOL) {
            return EHandlerResult::Continue;
        }

        if (targetEvent->GetReachable()) {
            YDebug() << "REQUIRED target found " << targetEvent->GetDir() << Endl;
            yMake->Conf.AddTarget(targetEvent->GetDir());
            yMake->AddStartTarget(targetEvent->GetDir());
        } else {
            YDebug() << "POSSIBLE target found " << targetEvent->GetDir() << Endl;
            yMake->AddTarget(targetEvent->GetDir());
        }

        return EHandlerResult::Continue;
    }

    EHandlerResult AllForeignPlatformsReportedEventHandler(const TString&, THolder<TYMake>&) {
        YDebug() << "DONE" << Endl;
        return EHandlerResult::AllDone;
    }

    void TServer::ProcessStreamBlocking(IInputStream& input, THolder<TYMake>& yMake) {
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
            if (handlerIt->second(line, yMake) == EHandlerResult::AllDone) {
                break;
            }
        }
    };
} // namespace NEvlogServer
