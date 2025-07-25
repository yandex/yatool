#include "vars.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/command_store.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/macro_processor.h>

TStringBuf Get1(const TYVar* var) {
    if (!var || var->empty())
        return TStringBuf();
    return (*var)[0].Name;
}

TStringBuf Eval1(const TYVar* var) {
    if (!Get1(var).empty()) {
        return GetCmdValue(Get1(var));
    }
    return ("");
}

TString GetAll(const TYVar* var) {
    if (!var || var->empty())
        return TString();
    TString result = "";
    for (const auto& part : *var) {
        if (result.size()) {
            result += " ";
        }
        result += part.Name;
    }
    return result;
}

TString EvalAll(const TYVar* var) {
    if (!var || var->empty())
        return TString();
    TString result = "";
    for (const auto& part : *var) {
        if (result.size()) {
            result += " ";
        }
        if (part.HasPrefix)
            result += GetCmdValue(part.Name);
        else
            result += part.Name;
    }
    return result;
}

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
