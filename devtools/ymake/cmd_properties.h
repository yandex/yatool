#pragma once

#include "vars.h"

#include <util/generic/map.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>

struct TKeyword {
    size_t From;
    size_t To;
    size_t Pos;
    TString DeepReplaceTo;
    TVector<TString> OnKwPresent;
    TVector<TString> OnKwMissing;
    TKeyword()
        : From(0)
        , To(0)
        , Pos(0)
    {
    }
    TKeyword(const TString& myName, size_t from, size_t to, const TString& deepReplaceTo, TStringBuf onKwPresent = {}, TStringBuf onKwMissing = {})
        : From(from)
        , To(to)
        , Pos(0)
        , DeepReplaceTo(deepReplaceTo)
    {
        if (onKwPresent.data() != nullptr) // "" from file is not NULL
            OnKwPresent.emplace_back(onKwPresent);
        else if (!From && !To)
            OnKwPresent.push_back(myName);
        if (onKwMissing.data() != nullptr)
            OnKwMissing.emplace_back(onKwMissing);
        //YDIAG(V) << "Added keyword: "  << From << "->" << To << " with deep_replace " << deep_replace_to << Endl;
    }

    Y_SAVELOAD_DEFINE(
        From,
        To,
        Pos,
        DeepReplaceTo,
        OnKwPresent,
        OnKwMissing
    );
};

class TCmdProperty {
public:
    using TMacroCall = std::pair<TString, TString>;
    using TMacroCalls = TVector<TMacroCall>;

    class TKeywords {
    public:
        TKeywords() noexcept = default;
        ~TKeywords() noexcept = default;

        TKeywords(const TKeywords&) = delete;
        TKeywords& operator=(const TKeywords&) = delete;
        TKeywords(TKeywords&&) noexcept = default;
        TKeywords& operator=(TKeywords&&) noexcept = default;


        void AddKeyword(const TString& word, size_t from, size_t to, const TString& deepReplaceTo, const TStringBuf& onKwPresent = nullptr, const TStringBuf& onKwMissing = nullptr);
        void AddArrayKeyword(const TString& word, const TString& deepReplaceTo) {
            AddKeyword(word, 0, ::Max<ssize_t>(), deepReplaceTo);
        }

        bool Empty() const noexcept {
            return Collected_.empty();
        }

        TVector<std::pair<TString, TKeyword>> Take() && noexcept {
            return std::move(Collected_);
        }
    private:
        TVector<std::pair<TString, TKeyword>> Collected_;
    };

    TCmdProperty() noexcept = default;
    TCmdProperty(const TVector<TString>& cmd, TKeywords&& kw);

    bool AddMacroCall(const TStringBuf& name, const TStringBuf& argList);

    static bool IsBaseMacroCall(const TStringBuf& name);

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

    const TVector<TString>& ArgNames() const noexcept {
        return ArgNames_;
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
    size_t Key2ArrayIndex(TStringBuf arg) const;

    bool HasKeyword(TStringBuf arg) const {
        return std::ranges::binary_search(Keywords_, arg, std::less<>{}, &std::pair<TString, TKeyword>::first);
    }
    bool IsNonPositional() const {
        return !Keywords_.empty();
    }
    size_t GetKeyArgsNum() const {
        return Keywords_.size();
    }
    TString GetDeepReplaceTo(size_t arrNum) const {
        if (arrNum < Keywords_.size()) {
            return Keywords_[arrNum].second.DeepReplaceTo;
        }
        return {};
    }
    const TVector<std::pair<TString, TKeyword>>& GetKeywords() const noexcept {
        return Keywords_;
    }
    const TString& GetKeyword(size_t arrNum) const {
        Y_ASSERT(arrNum < Keywords_.size());
        return Keywords_[arrNum].first;
    }
    const TKeyword& GetKeywordData(size_t arrNum) const {
        Y_ASSERT(arrNum < Keywords_.size());
        return Keywords_[arrNum].second;
    }
    const TKeyword* GetKeywordData(TStringBuf name) const {
        const auto [first, last] = std::ranges::equal_range(Keywords_, name, std::less<>{}, &std::pair<TString, TKeyword>::first);
        return first != last ? &first->second : nullptr;
    }

    bool HasUsrArgs() const noexcept {
        return NumUsrArgs_ != 0;
    }

    size_t GetNumUsrArgs() const noexcept {
        return NumUsrArgs_;
    }

    Y_SAVELOAD_DEFINE(
        HasConditions_,
        ArgNames_,
        MacroCalls_,
        SpecVars_,
        Keywords_,
        NumUsrArgs_
    );

private:
    bool HasConditions_ = false;
    //for macrocalls
    TVector<TString> ArgNames_;
    TMacroCalls MacroCalls_;
    TVars SpecVars_; //use only for inner scope
    // /me cries loudly because of absence of std::flat_map right here and right now.
    // Do not hesitate to remove this coment once proper time will come.
    TVector<std::pair<TString, TKeyword>> Keywords_;
    size_t NumUsrArgs_ = 0;
};
