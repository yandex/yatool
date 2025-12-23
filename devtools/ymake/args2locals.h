#pragma once

#include <devtools/ymake/macro_string.h>

#include <expected>
#include <string>

class TCmdProperty;

enum class EMapMacroVarsErrClass {
    UserSyntaxError,
    ArgsSequenceError
};
struct TMapMacroVarsErr {
    EMapMacroVarsErrClass ErrorClass;
    std::string Message;

    void Report(TStringBuf macroName, TStringBuf argsStr) const;
};

using TMapMacroVarsResult = std::expected<void, TMapMacroVarsErr>;

TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty* prop, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals);
TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty& macroProps, TArrayRef<const TStringBuf> args, TVars& locals);
