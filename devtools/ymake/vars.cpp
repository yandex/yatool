#include "vars.h"

TStringBuf Get1(const TYVar* var) {
    if (!var || var->empty())
        return TStringBuf();
    return (*var)[0].Name;
}

TStringBuf Eval1(const TYVar* var) {
    auto val = Get1(var);
    if (!val.empty()) {
        return GetCmdValue(val);
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
