#pragma once

#include <devtools/ymake/macro_processor.h>

namespace NCommands {

    struct TEvalCtx {
        const TVars& Vars;
        TCommandInfo& CmdInfo;
        const TVector<std::span<TVarStr>>& Inputs;
    };

    struct TTermError {
        TTermError(TString msg, bool origin): Msg(msg), Origin(origin) {}
        TString Msg;
        bool Origin;
    };
    struct TTermNothing {};

    struct TTaggedString {
        // TODO TStringBufs should be usable here (but we do not support evaluating string views yet)
        TString Data;
        TVector<TString> Tags = {};
    };
    using TTaggedStrings = TVector<TTaggedString>;

    using TTermValue = std::variant<TTermError, TTermNothing, TString, TVector<TString>, TTaggedStrings>;

}
