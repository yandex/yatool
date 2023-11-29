#pragma once

#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/diag/manager.h>

#include <google/protobuf/message.h>

#include <util/generic/string.h>
#include <util/folder/path.h>

#include "common_display/trace.h"

namespace NYMake {
    using TProtoMessage = ::google::protobuf::Message;

    void InitTraceSubsystem(const TString& events);
    bool TraceEnabled(ETraceEvent what) noexcept;
}

#define TRACE(W, M)                                                    \
    ConfMsgManager()->ReportConfigureEvent(::ETraceEvent::W, NYMake::EventToStr(M));  \

#define FORCE_TRACE(W, M)          \
    if (NYMake::TraceEnabled(::ETraceEvent::W)) { \
        NYMake::Trace(M);          \
    }

namespace NYMake {
    class TTraceStage {
    public:
        TTraceStage(const TString& stage)
            : Stage_(stage)
        {
            FORCE_TRACE(U, NEvent::TStageStarted(Stage_));
        }
        ~TTraceStage() {
            FORCE_TRACE(U, NEvent::TStageFinished(Stage_));
        }

    private:
        TString Stage_;
    };
}
