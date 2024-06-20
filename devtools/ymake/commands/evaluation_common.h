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
    using TTermValue = std::variant<TTermError, TTermNothing, TString, TVector<TString>>;

}
