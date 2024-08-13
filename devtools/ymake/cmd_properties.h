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

struct TCmdProperty: public TUnitProperty {
    TMap<TString, TKeyword> Keywords;
    TMap<size_t, TString> Position2Key;
    size_t NumUsrArgs = 0;

    TString ConvertCmdArgs(const TStringBuf& cmd);
    size_t Key2ArrayIndex(const TString& arg) const;
    void AddKeyword(const TString& word, size_t from, size_t to, const TString& deep_replace_to, const TStringBuf& onKwPresent = nullptr, const TStringBuf& onKwMissing = nullptr);
    void DesignateKeysPos();

    bool HasKeyword(const TString& arg) const {
        return Keywords.find(arg) != Keywords.end();
    }
    bool IsNonPositional() const {
        return !Keywords.empty();
    }
    size_t GetKeyArgsNum() const {
        return Keywords.size();
    }
    TString GetDeepReplaceTo(size_t arrNum) const {
        if (const auto pit = Position2Key.find(arrNum); pit != Position2Key.end()) {
            const auto kit = Keywords.find(pit->second);
            Y_ASSERT(kit != Keywords.end());
            return kit->second.DeepReplaceTo;
        }
        return "";
    }
    const TString& GetKeyword(size_t arrNum) const {
        Y_ASSERT(Position2Key.contains(arrNum));
        return Position2Key.find(arrNum)->second;
    }
    const TKeyword& GetKeywordData(size_t arrNum) const {
        return Keywords.find(GetKeyword(arrNum))->second;
    }

    TUnitProperty& GetBaseReference() {
        return *this;
    }

    const TUnitProperty& GetBaseReference() const {
        return *this;
    }

    Y_SAVELOAD_DEFINE(
        GetBaseReference(),
        Keywords,
        Position2Key,
        NumUsrArgs
    );
};
