#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/escape.h>
#include <span>

TVector<TVector<TString>> SplitCommandsAndArgs(TStringBuf cmd);
TVector<TString> SplitCommands(TStringBuf cmd);
TVector<TString> SplitArgs(TStringBuf cmd);

template<typename T, typename F>
TString JoinArgs(std::span<T> args, F proj) {
    auto result = TString();
    bool first = true;
    for (auto& arg : args) {
        if (!first)
            result += " ";
        result += "\"";
        EscapeC(proj(arg), result);
        result += "\"";
        first = false;
    }
    return result;
}
