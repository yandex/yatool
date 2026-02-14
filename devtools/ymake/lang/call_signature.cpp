#include "call_signature.h"

#include <devtools/ymake/options/static_options.h>
#include <devtools/ymake/diag/dbg.h>

#include <ranges>

TSignature::TSignature(const TVector<TString>& cmd, TSignature::TKeywords&& kw)
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

TStringBuf TSignature::GetVarargName() const noexcept {
    if (!HasVararg())
        return {};

    TStringBuf res{ArgNames_.back()};
    res.remove_suffix(3); // NStaticConf::ARRAY_SUFFIX.size() but ARRAY_SUFFIX is `const char*` :(
    return res;
}

bool TSignature::HasVararg() const noexcept {
    return NumUsrArgs_ != 0 && ArgNames_.back().EndsWith(NStaticConf::ARRAY_SUFFIX);
}

size_t TSignature::Key2ArrayIndex(TStringBuf arg) const {
    const auto [first, last] = std::ranges::equal_range(Keywords_, arg, std::less<>{}, &std::pair<TString, TKeyword>::first);
    AssertEx(first != last, "Arg was defined as keyword and must be in map.");
    return first->second.Pos;
}

void TSignature::TKeywords::AddKeyword(const TString& keyword, size_t from, size_t to, const TString& deepReplaceTo, const TStringBuf& onKwPresent, const TStringBuf& onKwMissing) {
    Collected_.emplace_back(keyword, TKeyword{keyword, from, to, deepReplaceTo, onKwPresent, onKwMissing});
}
