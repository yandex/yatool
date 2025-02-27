#include "cmd_properties.h"
#include "builtin_macro_consts.h"
#include "macro_processor.h"

#include <util/generic/hide_ptr.h>
#include <util/string/split.h>

#include <ranges>
using namespace std::literals;

namespace {

size_t CountOwnArgs(TStringBuf cmd) noexcept {
    size_t varEnd = 0;
    if (cmd.at(0) == '(' && (varEnd = FindMatchingBrace(cmd)) != TString::npos) { //has own args
        cmd = cmd.substr(0, varEnd);
        return std::ranges::distance(cmd | std::views::split(", "sv));
    }
    return 0;
}

}

TCmdProperty::TCmdProperty(TStringBuf cmd, TKeywords&& kw)
    : Keywords_{std::move(kw).Take()}
    , NumUsrArgs_{CountOwnArgs(cmd)}
{
    size_t cnt = 0;
    for (auto& [name, keyword]: Keywords_) {
        keyword.Pos = cnt;
        Position2Key_[cnt++] = name;
    }
}

TString TCmdProperty::ConvertCmdArgs(const TStringBuf& cmd) const {
    TString res;
    res = "(";
    for (const auto& [_, key]: Position2Key_)
        res += key + "..., ";
    return TString::Join(res, NumUsrArgs_ ? "" : ")", NumUsrArgs_ ? cmd.SubStr(1) : cmd);
}

void TCmdProperty::TKeywords::AddKeyword(const TString& keyword, size_t from, size_t to, const TString& deep_replace_to, const TStringBuf& onKwPresent, const TStringBuf& onKwMissing) {
    Collected_[keyword] = TKeyword(keyword, from, to, deep_replace_to, onKwPresent, onKwMissing);
}

size_t TCmdProperty::Key2ArrayIndex(const TString& arg) const {
    AssertEx(Keywords_.find(arg) != Keywords_.end(), "Arg was defined as keyword and must be in map.");
    return Keywords_.find(arg)->second.Pos;
}

bool TUnitProperty::AddMacroCall(const TStringBuf& name, const TStringBuf& argList) {
    MacroCalls.push_back(std::make_pair(TString{name}, SpecVars.size() ? TCommandInfo().SubstMacroDeeply(nullptr, argList, SpecVars, false) : TString{argList}));
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
        || name == NMacro::_LATE_GLOB
        || name == NMacro::_NEVERCACHE;
}
