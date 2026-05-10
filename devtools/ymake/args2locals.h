#pragma once

#include <devtools/ymake/macro_string.h>

#include <expected>
#include <string>

class TSignature;

struct TMapMacroVarsErr {
    std::string Message;

    void Report(TStringBuf macroName, TStringBuf argsStr) const;
};

using TMapMacroDiagResult = std::expected<void, TMapMacroVarsErr>;
using TMapMacroVarsResult = std::expected<TVars, TMapMacroVarsErr>;

TMapMacroDiagResult AddMacroArgsToLocals(const TSignature* sign, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals);
TMapMacroDiagResult AddMacroArgsToLocals(const TSignature& sign, TArrayRef<const TStringBuf> args, TVars& locals);
TMapMacroVarsResult AddMacroArgsToLocals(const TSignature& sign, TArrayRef<const TStringBuf> args);
