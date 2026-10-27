#include "reporter.h"
#include "manager.h"
#include "trace.h"

#include <util/generic/ptr.h>

namespace {
    class TDiagnosticStream final: public IOutputStream {
    public:
        TDiagnosticStream(IDiagnosticReporter& reporter, TDiagnosticMessage message)
            : Reporter_(reporter)
            , Message_(std::move(message))
        {
        }

        ~TDiagnosticStream() override {
            try {
                Reporter_.ReportMessage(std::move(Message_));
            } catch (...) {
            }
        }

    private:
        void DoWrite(const void* data, size_t size) override {
            Message_.Text.append(static_cast<const char*>(data), size);
        }

        IDiagnosticReporter& Reporter_;
        TDiagnosticMessage Message_;
    };
}

TAutoPtr<IOutputStream> MakeDiagnosticStream(IDiagnosticReporter& reporter, TDiagnosticMessage message) {
    return MakeHolder<TDiagnosticStream>(reporter, std::move(message)).Release();
}

IDiagnosticReporter* DiagnosticReporter() {
    if (const auto* context = CurrentContext<TExecContext>) {
        Y_ENSURE(context->DiagnosticReporter);
        return context->DiagnosticReporter.get();
    }
    return ConfMsgManager();
}

std::shared_ptr<IOutputStream> IDiagnosticReporter::ReportConfigureMessage(EConfMsgType type, TStringBuf kind, size_t row, size_t column) {
    const auto& [owner, name] = Diag()->Where.back();
    return std::shared_ptr<IOutputStream>(MakeDiagnosticStream(*this, {
        .Type = type, .Kind = TString(kind), .Text = {}, .Owner = owner,
        .OwnerName = name, .Row = row, .Column = column,
        .IsPersistent = Diag()->Persistency, .IsConfigure = true,
        .Cutoff = Display()->GetCutoff(), .Output = LockedStream(),
    }).Release());
}

void IDiagnosticReporter::ReportConfigureEvent(ETraceEvent what, const TString& event) {
    ReportDiagnosticEvent({what, event, Diag()->Where.back().first, Diag()->Persistency});
}

void EmitDiagnosticEvent(const TDiagnosticEvent& event) {
    if (NYMake::TraceEnabled(event.Type)) {
        NYMake::Trace(event.Event);
    }
}

void TBufferedDiagnosticReporter::ReportMessage(TDiagnosticMessage message) {
    if (message.IsConfigure && message.Type == EConfMsgType::Error) {
        HasConfigurationErrors = true;
    }
    Entries_.emplace_back(std::move(message));
}

void TBufferedDiagnosticReporter::ReportDiagnosticEvent(TDiagnosticEvent event) {
    Entries_.emplace_back(std::move(event));
}

void TBufferedDiagnosticReporter::Flush() {
    auto entries = std::move(Entries_);
    Entries_.clear();
    for (const auto& entry : entries) {
        std::visit([](const auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, TDiagnosticMessage>) {
                EmitDiagnosticEvent(Display()->EmitMessage(value));
            } else {
                EmitDiagnosticEvent(value);
            }
        }, entry);
    }
}
