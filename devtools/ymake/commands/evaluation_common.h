#pragma once

#include <devtools/ymake/macro_processor.h>

namespace NCommands {

    struct TEvalCtx {
        const TVars& Vars;
        TCommandInfo& CmdInfo;
        const TVector<std::span<TVarStr>>& Inputs;
    };

    using TTermValue = std::variant<std::monostate, TString, TVector<TString>>;

}
