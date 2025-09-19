#include "glob_helper.h"
#include "builtin_macro_consts.h"
#include "vardefs.h"

#include <devtools/ymake/diag/manager.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>

TGlobRestrictions TGlobHelper::ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro) {
    const auto rend = restrictions.cend();
    auto parseVal = [&macro, &rend](TArrayRef<const TStringBuf>::iterator& rit, const TStringBuf& name) {
        if (*rit != name) {
            return -1;
        }
        if ((rit + 1) == rend) {
            YConfErr(Syntax) << "No value for " << name << " in " << macro << Endl;
            return -2;
        }
        rit++;
        int v;
        if (!TryFromString(*rit, v)) {
            v = 0;
        }
        if (v <= 0 || v > 1000000) {
            YConfErr(Syntax) << "Invalid value '" << *rit << "' for " << name << " in " << macro << Endl;
            return -3;
        }
        return v;
    };

    TGlobRestrictions globRestrictions;
    auto rit = restrictions.cbegin();
    while (rit != rend) {
        auto maxMatches = parseVal(rit, NArgs::MAX_MATCHES);
        if (maxMatches > 0) {
            globRestrictions.MaxMatches = maxMatches;
        }
        auto maxWatchDirs = parseVal(rit, NArgs::MAX_WATCH_DIRS);
        if (maxWatchDirs > 0) {
            globRestrictions.MaxWatchDirs = maxWatchDirs;
        }
        if (maxMatches == -1 && maxWatchDirs == -1) {
            YConfErr(Syntax) << "Unknown restriction name " << *rit << " in " << macro << Endl;
        }
        rit++;
    }
    return globRestrictions;
}

template<typename V>
static void GetVal(const TString& var, const TStringBuf& val, V&value) {
    V v;
    if (!TryFromString(val, v)) {
        YConfWarn(Syntax) << "Invalid value '" << val << "' in " << var << Endl;
    } else {
        value = v;
    }
}

TString TGlobHelper::StringElemId(ui32 elemId) {
    return ToString(elemId);
}

TString TGlobHelper::GlobPatternElemIdsVar(const TStringBuf& strGlobVarElemId) {
    return TStringBuilder() << NArgs::GLOBVAR_PATTERN_ELEM_IDS << "-" << strGlobVarElemId;
}

static void SaveGlobVarElemId(TVars& vars, const TStringBuf& strGlobVarElemId) {
    const auto& strGlobVarElemIds = GetCmdValue(vars.Get1(NVariableDefs::VAR_GLOB_VAR_ELEM_IDS));
    if (strGlobVarElemIds.IsInited() && strGlobVarElemIds.size() >= strGlobVarElemId.size()) {
        TVector<TStringBuf> strElemIds;
        Split(strGlobVarElemIds, " ", strElemIds);
        if (Find(strElemIds, strGlobVarElemId) != strElemIds.end()) {
            return; // already appended
        }
    }
    vars.SetAppend(NVariableDefs::VAR_GLOB_VAR_ELEM_IDS, strGlobVarElemId);
}

void TGlobHelper::SaveGlobPatternElemIds(TVars& vars, const ui32 globVarElemId, const TVector<ui32>& globPatternElemIds) {
    auto strGlobVarElemId = StringElemId(globVarElemId);
    SaveGlobVarElemId(vars, strGlobVarElemId);
    vars.SetValue(GlobPatternElemIdsVar(strGlobVarElemId), JoinVectorIntoString(globPatternElemIds, " "));
}

static TVector<TStringBuf> LoadStrElemIds(const TVars& vars, const TString& varStrElemIds) {
    const auto strElemIdsVal = GetCmdValue(vars.Get1(varStrElemIds));
    TVector<TStringBuf> strElemIds;
    if (!strElemIdsVal.empty()) {
        Split(strElemIdsVal, " ", strElemIds);
    }
    return strElemIds;
}

TVector<TStringBuf> TGlobHelper::LoadStrGlobPatternElemIds(const TVars& vars, const TString& strGlobVarElemId) {
    return LoadStrElemIds(vars, GlobPatternElemIdsVar(strGlobVarElemId));
}

TVector<ui32> TGlobHelper::LoadGlobPatternElemIds(const TVars& vars, const ui32 globVarElemId) {
    const auto globPatternElemIdsVar = GlobPatternElemIdsVar(StringElemId(globVarElemId));
    const auto strGlobPatternElemIds = LoadStrElemIds(vars, globPatternElemIdsVar);
    TVector<ui32> globPatternElemIds;
    for (const auto& strGlobPatternElemId: strGlobPatternElemIds) {
        ui32 globPatternElemId;
        GetVal(globPatternElemIdsVar, strGlobPatternElemId, globPatternElemId);
        globPatternElemIds.push_back(globPatternElemId);
    }
    return globPatternElemIds;
}

TString TGlobHelper::MaxMatchesVar(const TStringBuf& strGlobVarElemId) {
    return TStringBuilder() << NArgs::MAX_MATCHES << "-" << strGlobVarElemId;
}

TString TGlobHelper::MaxWatchDirsVar(const TStringBuf& strGlobVarElemId) {
    return TStringBuilder() << NArgs::MAX_WATCH_DIRS << "-" << strGlobVarElemId;
}

void TGlobHelper::SaveGlobRestrictions(TVars& vars, const ui32 globVarElemId, const TGlobRestrictions& globRestrictions) {
    auto strGlobVarElemId = StringElemId(globVarElemId);
    SaveGlobVarElemId(vars, strGlobVarElemId);
    vars.SetValue(MaxMatchesVar(strGlobVarElemId), ToString(globRestrictions.MaxMatches));
    vars.SetValue(MaxWatchDirsVar(strGlobVarElemId), ToString(globRestrictions.MaxWatchDirs));
}

TGlobRestrictions TGlobHelper::LoadGlobRestrictions(const TVars& vars, const ui32 globVarElemId) {
    TGlobRestrictions globRestrictions;
    auto strGlobVarElemId = StringElemId(globVarElemId);
    const auto maxMatchesVar = MaxMatchesVar(strGlobVarElemId);
    const auto maxMatchesVal = GetCmdValue(vars.Get1(maxMatchesVar));
    if (!maxMatchesVal.empty()) {
        GetVal(maxMatchesVar, maxMatchesVal, globRestrictions.MaxMatches);
    }
    const auto maxWatchDirsVar = MaxWatchDirsVar(strGlobVarElemId);
    const auto maxWatchDirsVal = GetCmdValue(vars.Get1(maxWatchDirsVar));
    if (!maxWatchDirsVal.empty()) {
        GetVal(maxWatchDirsVar, maxWatchDirsVal, globRestrictions.MaxWatchDirs);
    }
    return globRestrictions;
}

TString TGlobHelper::MatchedVar(const TStringBuf& strGlobPatternElemId) {
    return TStringBuilder() << NArgs::MATCHED << "-" << strGlobPatternElemId;
}

TString TGlobHelper::SkippedVar(const TStringBuf& strGlobPatternElemId) {
    return TStringBuilder() << NArgs::SKIPPED << "-" << strGlobPatternElemId;
}

TString TGlobHelper::DirsVar(const TStringBuf& strGlobPatternElemId) {
    return TStringBuilder() << NArgs::DIRS << "-" << strGlobPatternElemId;
}

void TGlobHelper::SaveGlobPatternStat(TVars& vars, const ui32 globPatternElemId, const TGlobStat& globPatternStat) {
    auto strGlobPatternElemId = StringElemId(globPatternElemId);
    vars.SetValue(MatchedVar(strGlobPatternElemId), ToString(globPatternStat.MatchedFilesCount));
    vars.SetValue(SkippedVar(strGlobPatternElemId), ToString(globPatternStat.SkippedFilesCount));
    vars.SetValue(DirsVar(strGlobPatternElemId), ToString(globPatternStat.WatchedDirsCount));
}

TGlobStat TGlobHelper::LoadGlobPatternStat(const TVars& vars, const ui32 globPatternElemId) {
    auto strGlobPatternElemId = StringElemId(globPatternElemId);
    TGlobStat globPatternStat;
    const auto matchedVar = MatchedVar(strGlobPatternElemId);
    const auto matchedVal = GetCmdValue(vars.Get1(matchedVar));
    if (!matchedVal.empty()) {
        GetVal(matchedVar, matchedVal, globPatternStat.MatchedFilesCount);
    }
    const auto skipperVar = SkippedVar(strGlobPatternElemId);
    const auto skippedVal = GetCmdValue(vars.Get1(skipperVar));
    if (!skippedVal.empty()) {
        GetVal(skipperVar, skippedVal, globPatternStat.SkippedFilesCount);
    }
    const auto dirsVar = DirsVar(strGlobPatternElemId);
    const auto dirsVal = GetCmdValue(vars.Get1(dirsVar));
    if (!dirsVal.empty()) {
        GetVal(dirsVar, dirsVal, globPatternStat.WatchedDirsCount);
    }
    return globPatternStat;
}
