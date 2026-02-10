#include "cmd_properties.h"
#include "macro_processor.h"

#include <util/string/split.h>

using namespace std::literals;

TCmdProperty::TCmdProperty(const TVector<TString>& cmd, TSignature::TKeywords&& kw)
    : Signature_{cmd, std::move(kw)}
{}

TString TCmdProperty::ConvertCmdArgs() const {
    TString res = JoinStrings(Signature_.ArgNames(), ", ");
    // TODO: compatibility hack to be carefully removed
    if (!Signature_.GetKeywords().empty() && Signature_.GetKeywords().size() == Signature_.ArgNames().size())
        res += ", ";
    return res;
}

bool TCmdProperty::AddMacroCall(const TStringBuf& name, const TStringBuf& argList) {
    MacroCalls_.emplace_back(name, SpecVars_.size() ? TCommandInfo().SubstMacroDeeply(nullptr, argList, SpecVars_, false) : argList);
    return true;
}
