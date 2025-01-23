#pragma once

#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <util/stream/input.h>

namespace NEvlogServer {
    struct TTarget {
        TString Dir;
        TString Tag;
        bool FollowRecurses;

        bool operator==(const TTarget& other) const {
            return Dir == other.Dir && Tag == other.Tag && FollowRecurses == other.FollowRecurses;
        }
    };
} // namespace NEvlogServer

template<>
struct THash<NEvlogServer::TTarget> {
    size_t operator()(const NEvlogServer::TTarget& t) {
        return CombineHashes(
            CombineHashes(THash<TString>{}(t.Dir), THash<TString>{}(t.Tag)),
            static_cast<size_t>(t.FollowRecurses)
        );
    }
};

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
        TServer(ITargetConfigurator& Configurator, TBuildConfiguration& Conf) : Configurator_(Configurator), Conf_(Conf) {
            for (auto& dir : Conf.StartDirs) {
                ReachableTargets_.insert({dir, "", true});
            }
        };
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
        THashSet<TTarget> ReachableTargets_;
        THashSet<TString> PossibleTargets_;
    };
} // namespace NEvlogServer
