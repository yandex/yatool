#pragma once

#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <util/stream/input.h>

namespace NEvlogServer {
    enum class EHandlerResult {
        Continue,
        AllDone,
    };

    enum class EMode {
        Configure,
        CollectOnly,
    };

    class TServer {
    public:
        TServer(ITargetConfigurator& Configurator, TBuildConfiguration& Conf) : Configurator_(Configurator), Conf_(Conf) {};
        void ProcessStreamBlocking(IInputStream& input);

    private:
        EHandlerResult ForeignPlatformTargetEventHandler(const TString& line);
        EHandlerResult AllForeignPlatformsReportedEventHandler(const TString& line);
        EHandlerResult BypassConfigureEventHandler(const TString& line);

        const THashMap<TString, class std::function<EHandlerResult(const TString&)>> HandlerMap_ {
            {"NEvent.TAllForeignPlatformsReported", std::bind(&TServer::AllForeignPlatformsReportedEventHandler, this, std::placeholders::_1)},
            {"NEvent.TForeignPlatformTarget", std::bind(&TServer::ForeignPlatformTargetEventHandler, this, std::placeholders::_1)},
            {"NEvent.TBypassConfigure", std::bind(&TServer::BypassConfigureEventHandler, this, std::placeholders::_1)},
        };

        ITargetConfigurator& Configurator_;
        TBuildConfiguration& Conf_;
        TMaybe<EMode> Mode_;
        TVector<TString> ReachableTargets_;
        TVector<TString> PossibleTargets_;
    };
} // namespace NEvlogServer
