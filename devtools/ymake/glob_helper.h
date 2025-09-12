#pragma once

#include "vars.h"
#include <devtools/ymake/symbols/globs.h>

class TGlobHelper {
public:
    static TGlobRestrictions ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro);

    static TString StringElemId(ui32 elemId);

    static TString GlobPatternElemIdsVar(const TStringBuf& strGlobVarElemId);
    static void SaveGlobPatternElemIds(TVars& vars, const ui32 globVarElemId, const TVector<ui32>& globPatternElemIds);
    static TVector<TStringBuf> LoadStrGlobPatternElemIds(const TVars& vars, const TString& strGlobVarElemId);
    static TVector<ui32> LoadGlobPatternElemIds(const TVars& vars, const ui32 globVarElemId);

    static TString MaxMatchesVar(const TStringBuf& strGlobVarElemId);
    static TString MaxWatchDirsVar(const TStringBuf& strGlobVarElemId);
    static void SaveGlobRestrictions(TVars& vars, const ui32 globVarElemId, const TGlobRestrictions& globRestrictions);
    static TGlobRestrictions LoadGlobRestrictions(const TVars& vars, const ui32 globVarElemId);

    static TString MatchedVar(const TStringBuf& strGlobPatternElemId);
    static TString SkippedVar(const TStringBuf& strGlobPatternElemId);
    static TString DirsVar(const TStringBuf& strGlobPatternElemId);
    static void SaveGlobPatternStat(TVars& vars, const ui32 globPatternElemId, const TGlobStat& globPatternStat);
    static TGlobStat LoadGlobPatternStat(const TVars& vars, const ui32 globPatternElemId);
};
