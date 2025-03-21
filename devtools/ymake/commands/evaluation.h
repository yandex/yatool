#pragma once

#include <devtools/ymake/commands/compilation.h>
#include <devtools/ymake/lang/macro_values.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/vars.h>

namespace NCommands {

    struct TPreevalCtx {
        TMacroValues& Values;
        NCommands::TCompiledCommand& Sink;
    };

    struct TEvalCtx {
        const TBuildConfiguration& BuildConf;
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
