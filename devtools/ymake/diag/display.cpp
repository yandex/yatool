#include "display.h"

#include "trace.h"

#include <devtools/ymake/context_executor.h>

#include <util/stream/str.h>
#include <util/stream/file.h>
#include <util/stream/output.h>
#include <util/string/printf.h>
#include <util/string/builder.h>
#include <util/generic/singleton.h>
#include <util/string/cast.h>
#include <util/system/getpid.h>

#include <cstdlib>

using TStreamMessage = TDisplay::TStreamMessage;

void TDisplay::SetCutoff(EConfMsgType value) noexcept {
    if (auto* context = CurrentContext<TExecContext>) {
        context->DisplayCutoff = static_cast<unsigned int>(value);
    } else {
        FallbackCutoff_.store(value, std::memory_order_relaxed);
    }
}

EConfMsgType TDisplay::GetCutoff() const noexcept {
    if (const auto* context = CurrentContext<TExecContext>) {
        return static_cast<EConfMsgType>(context->DisplayCutoff);
    }
    return FallbackCutoff_.load(std::memory_order_relaxed);
}

const TDisplay::TMsgType TDisplay::msgTypesAsString[4] = {
    std::make_pair(TStringBuf("Error"), TStringBuf("bad")),
    std::make_pair(TStringBuf("Warn"), TStringBuf("warn")),
    std::make_pair(TStringBuf("Info"), TStringBuf("imp")),
    std::make_pair(TStringBuf("Debug"), TStringBuf("unimp"))};

TDiagnosticEvent TDisplay::EmitMessage(const TDiagnosticMessage& message) {
    static const ui32 pid = GetPID();
    const auto [type, mod] = msgTypesAsString[static_cast<ui32>(message.Type)];
    TString where = message.OwnerName;
    if (message.Row != 0 && message.Column != 0) {
        where += ":" + ToString(message.Row) + ":" + ToString(message.Column);
    }
    TString prefix = type + (message.Kind ? TString("[") + message.Kind + "]" : "") + ": ";
    if (message.OwnerName) {
        prefix += TString("in ") + where + ": ";
    }
    if (message.Type < message.Cutoff) {
        Y_ENSURE(message.Output);
        message.Output->Emit(prefix + message.Text);
    }
    NEvent::TDisplayMessage event;
    event.SetType(type.data());
    event.SetMod(mod.data());
    event.SetSub(message.Kind);
    event.SetMessage(message.Text);
    if (message.OwnerName) {
        event.SetWhere(message.OwnerName);
    }
    if (message.Row != 0 && message.Column != 0) {
        event.SetRow(message.Row);
        event.SetColumn(message.Column);
    }
    event.SetPID(pid);
    return {
        (message.Type == EConfMsgType::Error || message.Type == EConfMsgType::Warning) ? ETraceEvent::E : ETraceEvent::D,
        NYMake::EventToStr(event), message.Owner, message.IsPersistent,
    };
}

TStreamMessage TDisplay::NewConfMsg(EConfMsgType type, TStringBuf kind, TStringBuf path, size_t row, size_t column) {
    return MakeDiagnosticStream(*DiagnosticReporter(), {
        .Type = type, .Kind = TString(kind), .Text = {}, .Owner = Diag()->Where.back().first,
        .OwnerName = TString(path), .Row = row, .Column = column,
        .IsPersistent = Diag()->Persistency, .IsConfigure = false, .Cutoff = GetCutoff(), .Output = LockedStream(),
    });
}

TDisplay* Display() {
    return Singleton<TDisplay>();
}
