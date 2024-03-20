#pragma once

#include <devtools/ymake/macro_processor.h>

namespace NCommands {

    struct TEvalCtx {
        const TVars& Vars;
        TCommandInfo& CmdInfo;
    };

    using TTermValue = std::variant<std::monostate, TString, TVector<TString>>;

}
