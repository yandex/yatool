#include "var_eval.h"

#include <devtools/ymake/command_store.h>
#include <devtools/ymake/conf.h>

TVector<TString> EvalAll(const TYVar& var, const TVars& vars, const TCommands& commands, const TCmdConf& cmdConf, const TBuildConfiguration& conf) {
    TVector<TString> result;
    for (const auto& part : var) {
        if (part.StructCmdForVars) {
            auto& expr = *commands.Get(part.Name, &cmdConf);
            auto dummyCmdInfo = TCommandInfo(conf, nullptr, nullptr);
            auto scr = TCommands::SimpleCommandSequenceWriter()
                .Write(commands, expr, vars, {}, dummyCmdInfo, &cmdConf, conf)
                .Extract();
            for (auto& cmd : scr)
                for (auto& arg : cmd)
                    result.push_back(arg);
        } else {
            if (part.HasPrefix)
                result.push_back(TString(GetCmdValue(part.Name)));
            else
                result.push_back(part.Name);
        }
    }
    return result;
}

