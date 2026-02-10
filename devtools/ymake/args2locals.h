#pragma once

#include <devtools/ymake/macro_string.h>

#include <expected>
#include <string>

class TSignature;

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

TMapMacroVarsResult AddMacroArgsToLocals(const TSignature* sign, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals);
TMapMacroVarsResult AddMacroArgsToLocals(const TSignature& sign, TArrayRef<const TStringBuf> args, TVars& locals);
