#include "trace.h"

#include "display.h"
#include "common_display/trace_sink.h"

#include <devtools/ymake/diag/trace_type_enums.h_serialized.h>

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <library/cpp/protobuf/json/proto2json.h>

#include <util/datetime/base.h>

using namespace NYMake;

namespace {
    TTraceEvents AsTraceEvents(char what) {
        ETraceEvent val;
        if (!TryFromString(TStringBuf{&what, 1}, val)) {
            return TTraceEvents{};
        }
        return TTraceEvents{val};
    }

    struct TTracer: NYMake::ITraceSink {
        TTracer() noexcept = default;

        inline void Init(TStringBuf events) noexcept {
            Enable = std::accumulate(
                events.begin(),
                events.end(),
                TTraceEvents{},
                [] (TTraceEvents val, char what) {return val | AsTraceEvents(what);});
        }

        inline bool Enabled(ETraceEvent what) const noexcept {
            return Enable & what;
        }

        void Trace(const TString& ev) override {
            LockedStream()->Emit(ev + '\n');
        }

        void SetStream(TAtomicSharedPtr<IOutputStream> stream) {
            LockedStream()->SetStream(stream);
        }

        TTraceEvents Enable;
    };

    NYMake::TScopedTraceSink<TTracer> GLOBAL_TRACER;
}

ITraceSink* NYMake::TTraceSinkScope::Current_ = nullptr;

void NYMake::InitTraceSubsystem(const TString& events) {
    GLOBAL_TRACER.Init(events);
}

bool NYMake::TraceEnabled(ETraceEvent what) noexcept {
    return GLOBAL_TRACER.Enabled(what);
}

void NYMake::SetTraceOutputStream(TAtomicSharedPtr<IOutputStream> stream) {
    GLOBAL_TRACER.SetStream(stream);
}
