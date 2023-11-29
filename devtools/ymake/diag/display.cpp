#include "display.h"

#include "trace.h"

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

namespace {
    class TChildOutputStream: public TString, public TStringOutput {
    public:
        using TFlusher = std::function<void(TString)>;

    public:
        TChildOutputStream(TFlusher&& flusher)
            : TStringOutput(*static_cast<TString*>(this))
            , Flusher(std::move(flusher))
        {
        }

        ~TChildOutputStream() override {
            try {
                this->Flush();
                Flusher(*this);
            } catch (...) {
            }
        }

    private:
        TFlusher Flusher;
    };
}

void TDisplay::SetStream(TLockedStream* stream) {
    this->Stream = stream;
}

TDisplay::TMsgType TDisplay::msgTypesAsString[4] = {
    std::make_pair(TStringBuf("Error"), TStringBuf("bad")),
    std::make_pair(TStringBuf("Warn"),  TStringBuf("warn")),
    std::make_pair(TStringBuf("Info"),  TStringBuf("imp")),
    std::make_pair(TStringBuf("Debug"), TStringBuf("unimp"))
};

TStreamMessage TDisplay::PrepareStream(EConfMsgType msgType, TStringBuf sub, TStringBuf path, size_t row, size_t column) {
    static const ui32 pid = GetPID();

    const auto [type, mod] = msgTypesAsString[static_cast<ui32>(msgType)];
    TString where = ToString(path);
    if (row != 0 && column != 0) {
        where += ":" + ToString(row) + ":" + ToString(column);
    }

    TString prefix = type + (sub ? TString("[") + sub + "]" : "") + ": ";
    if (path) {
        prefix += TString("in ") + where + ": ";
    }

    TStreamMessage stream = new TChildOutputStream(
        [this, prefix, msgType, type = type, mod = mod, sub, path, row, column, where](const TString& s) {
            if (this->Stream != nullptr && msgType <= MinHumanOutSeverity) {
                this->Stream->Emit(prefix + s);
            }
            NEvent::TDisplayMessage msg;
            msg.SetType(type.data());
            msg.SetMod(mod.data());
            msg.SetSub(sub ? sub.data() : "");
            msg.SetMessage(s);
            if (path) {
                msg.SetWhere(path.data());
            }
            if (row != 0 && column != 0) {
                msg.SetRow(row);
                msg.SetColumn(column);
            }
            msg.SetPID(pid);
            ConfMsgManager()->ReportConfigureEvent(
                (msgType == EConfMsgType::Error || msgType == EConfMsgType::Warning) ? ETraceEvent::E : ETraceEvent::D,
                NYMake::EventToStr(msg)
            );
        }
    );
    return stream;
}

TStreamMessage TDisplay::NewConfMsg(EConfMsgType type, TStringBuf msg, TStringBuf path, size_t row, size_t column) {
    return PrepareStream(type, msg, path, row, column);
}

TDisplay* Display() {
    return Singleton<TDisplay>();
}
