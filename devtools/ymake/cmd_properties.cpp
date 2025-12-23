#include "cmd_properties.h"
#include "builtin_macro_consts.h"
#include "macro_processor.h"

#include <devtools/ymake/options/static_options.h>

#include <util/generic/hide_ptr.h>
#include <util/string/split.h>

#include <ranges>
using namespace std::literals;

TCmdProperty::TCmdProperty(const TVector<TString>& cmd, TKeywords&& kw)
    : Keywords_{std::move(kw).Take()}
    , NumUsrArgs_{cmd.size()}
{
    std::ranges::sort(Keywords_, std::less<>{}, &std::pair<TString, TKeyword>::first);
    size_t cnt = 0;
    for (auto& [key, kw]: Keywords_) {
        kw.Pos = cnt++;
        ArgNames_.push_back(key + NStaticConf::ARRAY_SUFFIX);
    }

    for (const auto& name: cmd)
        ArgNames_.push_back(name);
}

TString TCmdProperty::ConvertCmdArgs() const {
    TString res = JoinStrings(ArgNames_, ", ");
    // TODO: compatibility hack to be carefully removed
    if (!Keywords_.empty() && Keywords_.size() == ArgNames_.size())
        res += ", ";
    return res;
}

void TCmdProperty::TKeywords::AddKeyword(const TString& keyword, size_t from, size_t to, const TString& deepReplaceTo, const TStringBuf& onKwPresent, const TStringBuf& onKwMissing) {
    Collected_.emplace_back(keyword, TKeyword{keyword, from, to, deepReplaceTo, onKwPresent, onKwMissing});
}

size_t TCmdProperty::Key2ArrayIndex(TStringBuf arg) const {
    const auto [first, last] = std::ranges::equal_range(Keywords_, arg, std::less<>{}, &std::pair<TString, TKeyword>::first);
    AssertEx(first != last, "Arg was defined as keyword and must be in map.");
    return first->second.Pos;
}

bool TCmdProperty::AddMacroCall(const TStringBuf& name, const TStringBuf& argList) {
    MacroCalls_.emplace_back(name, SpecVars_.size() ? TCommandInfo().SubstMacroDeeply(nullptr, argList, SpecVars_, false) : argList);
    return true;
}

bool TCmdProperty::IsBaseMacroCall(const TStringBuf& name) {
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
