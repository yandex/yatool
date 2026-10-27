#pragma once

#include "display.h"
#include "trace_type_enums.h"

#include <devtools/ymake/symbols/elem_id.h>

#include <util/generic/vector.h>
#include <variant>

struct TDiagnosticMessage {
    EConfMsgType Type;
    TString Kind;
    TString Text;
    TFileElemId Owner;
    TString OwnerName;
    size_t Row = 0;
    size_t Column = 0;
    bool IsPersistent = true;
    bool IsConfigure = false;
    EConfMsgType Cutoff = EConfMsgType::Count;
    NCommonDisplay::TLockedStream* Output = nullptr;
};

struct TDiagnosticEvent {
    ETraceEvent Type;
    TString Event;
    TFileElemId Owner;
    bool IsPersistent = true;
};

class IDiagnosticReporter {
public:
    virtual ~IDiagnosticReporter() = default;
    virtual void ReportMessage(TDiagnosticMessage message) = 0;
    virtual void ReportEvent(TDiagnosticEvent event) = 0;

    std::shared_ptr<IOutputStream> ReportConfigureMessage(EConfMsgType type, TStringBuf kind, size_t row = 0, size_t column = 0);
    void ReportConfigureEvent(ETraceEvent what, const TString& event);

    mutable bool HasConfigurationErrors = false;
};

IDiagnosticReporter* DiagnosticReporter();
TAutoPtr<IOutputStream> MakeDiagnosticStream(IDiagnosticReporter& reporter, TDiagnosticMessage message);
void EmitDiagnosticEvent(const TDiagnosticEvent& event);

class TBufferedDiagnosticReporter final: public IDiagnosticReporter {
public:
    void ReportMessage(TDiagnosticMessage message) override;
    void ReportEvent(TDiagnosticEvent event) override;
    void Flush();

private:
    TVector<std::variant<TDiagnosticMessage, TDiagnosticEvent>> Entries_;
};
