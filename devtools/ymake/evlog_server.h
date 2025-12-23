#pragma once

#include <devtools/ymake/foreign_platforms/io.h>
#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <util/stream/input.h>

#include <asio/thread_pool.hpp>

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
    enum class EMode {
        Configure,
        CollectOnly,
    };

    class TServer {
    public:
        TServer(TConfigurationExecutor exec, ITargetConfigurator& Configurator, TBuildConfiguration& Conf) : Configurator_(Configurator), Conf_(Conf), Exec_{exec} {
            for (auto& dir : Conf.StartDirs) {
                ReachableTargets_.insert({dir, "", true});
            }
        }
        asio::awaitable<void> ProcessStreamBlocking(NForeignTargetPipeline::TLineReader& reader);

    private:
        asio::awaitable<void> ForeignPlatformTargetEventHandler(const TString& line);
        void AllForeignPlatformsReportedEventHandler(const TString& line);
        asio::awaitable<void> BypassConfigureEventHandler(const TString& line);

        ITargetConfigurator& Configurator_;
        TBuildConfiguration& Conf_;
        TMaybe<EMode> Mode_;
        THashSet<TTarget> ReachableTargets_;
        THashSet<TString> PossibleTargets_;
        TConfigurationExecutor Exec_;
    };
} // namespace NEvlogServer
