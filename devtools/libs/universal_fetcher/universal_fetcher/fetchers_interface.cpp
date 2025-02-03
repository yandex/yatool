#include "fetchers_interface.h"

#include <library/cpp/logger/null.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/scope.h>
#include <util/string/builder.h>

namespace NUniversalFetcher {

    void TTransportRequest::ToJson(NJson::TJsonValue& ret) const {
        ret["transport"] = TransportName;
        ret["endpoint"] = Endpoint;
        ret["start_time_us"] = StartTime.MicroSeconds();
        ret["duration_us"] = Duration.MicroSeconds();
        ret["error"] = Error ? NJson::TJsonValue(*Error) : NJson::TJsonValue::UNDEFINED;
        ret["attrs"] = Attrs;
    }

    void TResourceInfo::ToJson(NJson::TJsonValue& ret) const {
        ret["filename"] = FileName;
        ret["executable"] = Executable;
        ret["size"] = Size;
        ret["checksum"] = NJson::TJsonValue::UNDEFINED;
        if (Checksum) {
            Checksum->ToJson(ret["checksum"]);
        }
        ret["attrs"] = Attrs;
    }

    void TFetchResult::ToJson(NJson::TJsonValue& ret) const {
        ret["status"] = ToString(Status);
        ret["error"] = Error ? NJson::TJsonValue(*Error) : NJson::TJsonValue::UNDEFINED;
        ResourceInfo.ToJson(ret["resource_info"]);
        auto& history = ret["transport_history"];
        history = NJson::TJsonArray();
        for (auto& el : TransportHistory) {
            history.AppendValue({});
            el.ToJson(history.Back());
        }
        ret["attrs"] = Attrs;
    }

}
