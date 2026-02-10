#pragma once

#include "vars.h"

#include <devtools/ymake/lang/call_signature.h>

#include <util/generic/map.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>

class TCmdProperty {
public:
    using TMacroCall = std::pair<TString, TString>;
    using TMacroCalls = TVector<TMacroCall>;

    TCmdProperty() noexcept = default;
    TCmdProperty(const TVector<TString>& cmd, TSignature::TKeywords&& kw);

    bool AddMacroCall(const TStringBuf& name, const TStringBuf& argList);

    void Inherit(const TCmdProperty& parent) {
        //TODO: fix bad and slow insertion
        if (parent.MacroCalls_.size()) {
            MacroCalls_.insert(MacroCalls_.begin(), parent.MacroCalls_.begin(), parent.MacroCalls_.end());
        }
    }

    void SetSpecVar(const TString& name, const TString& value) {
        SpecVars_.SetValue(name, value);
    }
    bool IsAllowedInLintersMake() const {
        return SpecVars_.Has(NProperties::ALLOWED_IN_LINTERS_MAKE);
    }

    const TSignature& Signature() const noexcept {
        return Signature_;
    }

    const TMacroCalls& GetMacroCalls() const noexcept {
        return MacroCalls_;
    }

    bool HasMacroCalls() const noexcept {
        return !MacroCalls_.empty();
    }

    void SetHasConditions(bool value) noexcept {
        HasConditions_ = value;
    }
    bool HasConditions() const noexcept {
        return HasConditions_;
    }

    TString ConvertCmdArgs() const;

    Y_SAVELOAD_DEFINE(
        HasConditions_,
        Signature_,
        MacroCalls_,
        SpecVars_
    );

private:
    bool HasConditions_ = false;
    //for macrocalls
    TSignature Signature_;
    TMacroCalls MacroCalls_;
    TVars SpecVars_; //use only for inner scope
};
