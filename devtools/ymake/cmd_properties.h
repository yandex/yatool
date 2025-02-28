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
    TKeyword(const TString& myName, size_t from, size_t to, const TString& deep_replace_to, const TStringBuf& onKwPresent = nullptr, const TStringBuf& onKwMissing = nullptr)
        : From(from)
        , To(to)
        , Pos(0)
        , DeepReplaceTo(deep_replace_to)
    {
        if (onKwPresent.data() != nullptr) // "" from file is not NULL
            OnKwPresent.push_back(TString{onKwPresent});
        else if (!From && !To)
            OnKwPresent.push_back(myName);
        if (onKwMissing.data() != nullptr)
            OnKwMissing.push_back(TString{onKwMissing});
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

struct TUnitProperty {
    bool HasConditions = false;
    //for macrocalls
    TVector<TString> ArgNames;
    bool HasMacroCalls = false;

    using TMacroCall = std::pair<TString, TString>;
    using TMacroCalls = TVector<TMacroCall>;
    TMacroCalls MacroCalls;
    TVars SpecVars; //use only for inner scope
    bool AddMacroCall(const TStringBuf& name, const TStringBuf& argList);
    void AddArgNames(const TString& argNamesList);

    static bool IsBaseMacroCall(const TStringBuf& name);

    void Inherit(const TUnitProperty& parent) {
        //TODO: fix bad and slow insertion
        if (parent.MacroCalls.size()) {
            MacroCalls.insert(MacroCalls.begin(), parent.MacroCalls.begin(), parent.MacroCalls.end());
            HasMacroCalls = true;
        }
    }

    Y_SAVELOAD_DEFINE(
        HasConditions,
        ArgNames,
        HasMacroCalls,
        MacroCalls,
        SpecVars
    );
};

class TCmdProperty: public TUnitProperty {
public:
    class TKeywords {
    public:
        TKeywords() noexcept = default;
        ~TKeywords() noexcept = default;

        TKeywords(const TKeywords&) = delete;
        TKeywords& operator=(const TKeywords&) = delete;
        TKeywords(TKeywords&&) noexcept = default;
        TKeywords& operator=(TKeywords&&) noexcept = default;


        void AddKeyword(const TString& word, size_t from, size_t to, const TString& deep_replace_to, const TStringBuf& onKwPresent = nullptr, const TStringBuf& onKwMissing = nullptr);

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
    TCmdProperty(TStringBuf cmd, TKeywords&& kw);

    TString ConvertCmdArgs(TStringBuf cmd) const;
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

    TUnitProperty& GetBaseReference() {
        return *this;
    }

    const TUnitProperty& GetBaseReference() const {
        return *this;
    }

    Y_SAVELOAD_DEFINE(
        GetBaseReference(),
        Keywords_,
        NumUsrArgs_
    );

private:
    // /me cries loudly because of absence of std::flat_map right here and right now.
    // Do not hesitate to remove this coment once proper time will come.
    TVector<std::pair<TString, TKeyword>> Keywords_;
    size_t NumUsrArgs_ = 0;
};
