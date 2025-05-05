#pragma once

#include <devtools/ymake/macro_string.h>

#include <expected>

class TCmdProperty;

std::expected<void, TMapMacroVarsErr> AddMacroArgsToLocals(const TCmdProperty* prop, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals, IMemoryPool& memPool);
void AddMacroArgsToLocals(const TCmdProperty& macroProps, TArrayRef<const TStringBuf> args, TVars& locals, IMemoryPool& memPool);
