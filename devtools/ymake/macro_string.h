#pragma once

#include "macro.h"

#include <util/generic/fwd.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

struct TVars;

// Prededined properties
const TStringBuf MULTIMODULE_PROP_NAME = TStringBuf("MULTIMODULE");
const TStringBuf VAR_MODULE_TAG = TStringBuf("MODULE_TAG");
const TStringBuf NEVERCACHE_PROP = TStringBuf("NEVER=CACHE");

// id:name=value ("id" is usually a module id, sometimes a node's ElemId, see FormatCmd invocations)
TString FormatCmd(ui64 id, const TStringBuf& name, const TStringBuf& value);
void ParseCmd(const TStringBuf& source, ui64& id, TStringBuf& cmdName, TStringBuf& cmdValue);
ui64 GetId(const TStringBuf& cmd);
TStringBuf SkipId(const TStringBuf& cmd);
TStringBuf GetCmdName(const TStringBuf& cmd);
TStringBuf GetCmdValue(const TStringBuf& cmd);

// + intent self-documentation wrappers
inline void ParseLegacyCommandOrSubst(const TStringBuf& source, ui64& id, TStringBuf& cmdName, TStringBuf& cmdValue) {
    // EDT_BuildCommand -> EMNT_BuildCommand
    // EDT_Include -> EMNT_BuildCommand
    // etc. (TBD)
    ParseCmd(source, id, cmdName, cmdValue);
}
inline void ParseCommandLikeProperty(const TStringBuf& source, ui64& id, TStringBuf& cmdName, TStringBuf& cmdValue) {
    // EDT_Property -> EMNT_BuildCommand
    ParseCmd(source, id, cmdName, cmdValue);
}
inline void ParseCommandLikeVariable(const TStringBuf& source, ui64& id, TStringBuf& cmdName, TStringBuf& cmdValue) {
    ParseCmd(source, id, cmdName, cmdValue);
}

// name=value
TString FormatProperty(const TStringBuf& name, const TStringBuf& value);
TStringBuf GetPropertyName(const TStringBuf& cmd);
TStringBuf GetPropertyValue(const TStringBuf& prop);

// (arg1, arg2, arg3)...
bool SplitArgs(const TStringBuf&, TVector<TStringBuf>& argNames);

// (arg1, arg2, arg3)
bool SplitMacroArgs(const TStringBuf&, TVector<TMacro>& args);

// name
// name(arg1 arg2 arg3)...
bool ParseMacroCall(const TStringBuf& macroCall, TStringBuf& name, TVector<TStringBuf>& args);

// "lit ${tolower:Name} $Options" -> ["Name"(ToLower), "Options"]
EMacroType GetMacrosFromPattern(const TStringBuf& pat, TVector<TMacroData>& macros, bool hasPrefix);

//like SubstGlobal(substval, " ", parDelim)
bool BreakQuotedEval(TString& substval, const TStringBuf& parDelim, bool allSpace, bool inQuote = false);
char BreakQuotedExec(TString& substval, const TStringBuf& parDelim, bool allSpace, char topQuote = 0);

// ["X", "Y", "Z"], ["A", "B", "C"] -> ["X=A", "Y=B", "Z=C"]
bool MapMacroVars(const TVector<TMacro>& args, const TVector<TStringBuf>& argNames, TVars& vars, const TStringBuf& argsStr);

// Takes source line starting with opening brace and finds position of
// matching closing brace of the same kind (skips inner matching pairs)
size_t FindMatchingBrace(const TStringBuf& source, size_t leftPos = 0) noexcept;

inline EMacroType GetMacroType(const TStringBuf& cmdText) {
    // tmp hack
    if (cmdText.at(0) != '(')
        return EMT_Usual;
    if (cmdText.back() == ')' && FindMatchingBrace(cmdText) == cmdText.size() - 1)
        return EMT_MacroCall;
    return EMT_MacroDef;
}

inline TStringBuf MacroDefBody(const TStringBuf& cmdText) {
    size_t lb = FindMatchingBrace(cmdText);
    if (lb != TString::npos)
        return cmdText.SubStr(lb + 1);
    return cmdText;
}

template<typename T>
std::pair<TArrayRef<T>, TArrayRef<T>> SplitBy(TArrayRef<T> span, std::add_const_t<T>& val) noexcept(noexcept(span.front() == val)) {
    T* first = span.data();
    T* last = first + span.size();
    T* pos = Find(first, last, val);
    return std::make_pair(TArrayRef<T>{first, pos}, TArrayRef<T>{pos, last});
}
