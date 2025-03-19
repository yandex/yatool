#pragma once

#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/common/cyclestimer.h>

#include <google/protobuf/message.h>

#include <util/generic/string.h>
#include <util/folder/path.h>

#include "common_display/trace.h"

namespace NYMake {
    using TProtoMessage = ::google::protobuf::Message;

    void InitTraceSubsystem(const TString& events);
    bool TraceEnabled(ETraceEvent what) noexcept;
    void SetTraceOutputStream(TAtomicSharedPtr<IOutputStream>);
}

#define TRACE(W, M)                                                    \
    ConfMsgManager()->ReportConfigureEvent(::ETraceEvent::W, NYMake::EventToStr(M));  \

#define FORCE_TRACE(W, M)          \
    if (NYMake::TraceEnabled(::ETraceEvent::W)) { \
        NYMake::Trace(M);          \
    }

namespace NYMake {
    class [[nodiscard]] TTraceStage {
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
        const TString Stage_;
    };

    class [[nodiscard]] TTraceStageWithTimer : public TTraceStage {
    public:
        TTraceStageWithTimer(const TString& stage, const TString& monName)
            : TTraceStage(stage)
            , MonName_(monName)
            , Timer_()
        {}

        ~TTraceStageWithTimer() {
            NStats::TStatsBase::MonEvent(MonName_, Timer_.GetSeconds());
        }
    private:
        const TString MonName_;
        TCyclesTimer Timer_;
    };
}
