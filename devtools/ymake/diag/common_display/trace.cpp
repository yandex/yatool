#include "trace.h"

#include "display.h"
#include "trace_sink.h"

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>
#include <library/cpp/protobuf/json/proto2json.h>
#include <util/datetime/base.h>

namespace NYMake{
    TString EventToStr(const TProtoMessage& ev) {
        using namespace NJson;

        TJsonValue v;
        NProtobufJson::Proto2Json(ev, v);

        v["_typename"] = ev.GetTypeName();
        v["_timestamp"] = Now().MicroSeconds();

        TString out;
        TStringOutput sout(out);

        TJsonWriter w(&sout, false, true, true);
        w.Write(&v);
        w.Flush();   // Otherwise out below may be empty
        return out;
    }

    void Trace(const TProxyEvent& ev) {
        TTraceSinkScope::CurrentSink()->Trace(EventToStr(ev));
    }

    void Trace(const TString& ev) {
        TTraceSinkScope::CurrentSink()->Trace(ev);
    }
};
