#include "cmd_properties.h"
#include "builtin_macro_consts.h"
#include "macro_processor.h"

#include <util/generic/hide_ptr.h>
#include <util/string/split.h>

TString TCmdProperty::ConvertCmdArgs(const TStringBuf& cmd) {
    TString res;
    size_t varEnd = 0;
    if (cmd.at(0) == '(' && (varEnd = FindMatchingBrace(cmd)) != TString::npos) { //has own args
        TVector<TStringBuf> args;
        Split(cmd.SubStr(0, varEnd), ", ", args);
        NumUsrArgs = args.size();
    }
    res = "(";

    DesignateKeysPos();
    TVector<TStringBuf> keyargs;
    keyargs.resize(GetKeyArgsNum());

    for (TMap<size_t, TString>::iterator key = Position2Key.begin(); key != Position2Key.end(); key++)
        res += key->second + "..." + (key != Position2Key.end() || NumUsrArgs ? ", " : "");
    return TString::Join(res, NumUsrArgs ? "" : ")", NumUsrArgs ? cmd.SubStr(1) : cmd);
}

void TCmdProperty::AddKeyword(const TString& keyword, size_t from, size_t to, const TString& deep_replace_to, const TStringBuf& onKwPresent, const TStringBuf& onKwMissing) {
    Keywords[keyword] = TKeyword(keyword, from, to, deep_replace_to, onKwPresent, onKwMissing);
}

void TCmdProperty::DesignateKeysPos() {
    size_t cnt = 0;
    for (TMap<TString, TKeyword>::iterator key = Keywords.begin(); key != Keywords.end(); key++) {
        key->second.Pos = cnt;
        Position2Key[cnt++] = key->first;
    }
}

size_t TCmdProperty::Key2ArrayIndex(const TString& arg) const {
    AssertEx(Keywords.find(arg) != Keywords.end(), "Arg was defined as keyword and must be in map.");
    return Keywords.find(arg)->second.Pos;
}

bool TUnitProperty::AddMacroCall(const TStringBuf& name, const TStringBuf& argList) {
    MacroCalls.push_back(std::make_pair(TString{name}, SpecVars.size() ? TCommandInfo(nullptr, nullptr, nullptr).SubstMacroDeeply(nullptr, argList, SpecVars, false) : TString{argList}));
    return true;
}

void TUnitProperty::AddArgNames(const TString& argNamesList) {
    if (! argNamesList.size())
        return;
    Split(argNamesList, ", ", ArgNames);
}

bool TUnitProperty::IsBaseMacroCall(const TStringBuf& name) {
    return name == NMacro::SET
        || name == NMacro::SET_APPEND
        || name == NMacro::SET_APPEND_WITH_GLOBAL
        || name == NMacro::DEFAULT
        || name == NMacro::ENABLE
        || name == NMacro::DISABLE
        || name == NMacro::_GLOB
        || name == NMacro::_LATE_GLOB;
}
