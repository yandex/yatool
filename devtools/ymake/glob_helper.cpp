#include "glob_helper.h"
#include "builtin_macro_consts.h"
#include "module_state.h"

#include <devtools/ymake/diag/manager.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>

static const TVector<ui32> EmptyElemIds;
static const TGlobRestrictions DefaultGlobRestrictions;
static const TGlobStat EmptyGlobStat;

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

void TGlobHelper::SaveGlobPatternElemIds(TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId, TVector<ui32>&& globPatternElemIds) {
    moduleGlobsData.GlobVars[globVarElemId].PatternElemIds = std::move(globPatternElemIds);
}

const TVector<ui32>& TGlobHelper::GetGlobPatternElemIds(const TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId) {
    if (auto it = moduleGlobsData.GlobVars.find(globVarElemId); it != moduleGlobsData.GlobVars.end()) {
        return it->second.PatternElemIds;
    }
    return EmptyElemIds;
}

void TGlobHelper::SaveGlobRestrictions(TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId, TGlobRestrictions&& globRestrictions) {
    moduleGlobsData.GlobVars[globVarElemId].GlobRestrictions = std::move(globRestrictions);
}

const TGlobRestrictions& TGlobHelper::GetGlobRestrictions(const TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId) {
    if (auto it = moduleGlobsData.GlobVars.find(globVarElemId); it != moduleGlobsData.GlobVars.end()) {
        return it->second.GlobRestrictions;
    }
    return DefaultGlobRestrictions;
}

void TGlobHelper::SaveGlobPatternStat(TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId, TGlobStat&& globPatternStat) {
    moduleGlobsData.GlobPatternStats[globPatternElemId] = std::move(globPatternStat);
}

const TGlobStat& TGlobHelper::GetGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId) {
    if (auto it = moduleGlobsData.GlobPatternStats.find(globPatternElemId); it != moduleGlobsData.GlobPatternStats.end()) {
        return it->second;
    }
    return EmptyGlobStat;
}

TGlobStat TGlobHelper::LoadGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId) {
    return GetGlobPatternStat(moduleGlobsData, globPatternElemId);
}
