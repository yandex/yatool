#pragma once

#include <devtools/ymake/ymake.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <util/stream/input.h>

namespace NEvlogServer {
    enum class EHandlerResult {
        Continue,
        AllDone,
    };

    EHandlerResult ForeignPlatformTargetEventHandler(const TString& line, THolder<TYMake>& yMake);
    EHandlerResult AllForeignPlatformsReportedEventHandler(const TString& line, THolder<TYMake>& yMake);

    class TServer {
    public:
        void ProcessStreamBlocking(IInputStream& input, THolder<TYMake>& yMake);

    private:
        const THashMap<TString, class std::function<EHandlerResult(const TString&, THolder<TYMake>&)>> HandlerMap_ {
            {"NEvent.TAllForeignPlatformsReported", AllForeignPlatformsReportedEventHandler},
            {"NEvent.TForeignPlatformTarget", ForeignPlatformTargetEventHandler},
        };
    };
} // namespace NEvlogServer
