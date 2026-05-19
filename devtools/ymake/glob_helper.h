#pragma once

#include <devtools/ymake/symbols/globs.h>

class TModule;

class TGlobHelper {
public:
    static TGlobRestrictions ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro);

    static void SaveGlobPatternElemIds(TModuleGlobsData& moduleGlobsData, const TCmdElemId globVarElemId, TVector<TCmdElemId>&& globPatternElemIds);
    static const TVector<TCmdElemId>& GetGlobPatternElemIds(const TModuleGlobsData& moduleGlobsData, const TCmdElemId globVarElemId);

    static void SaveGlobRestrictions(TModuleGlobsData& moduleGlobsData, const TCmdElemId globVarElemId, TGlobRestrictions&& globRestrictions);
    static const TGlobRestrictions& GetGlobRestrictions(const TModuleGlobsData& moduleGlobsData, const TCmdElemId globVarElemId);

    static void SaveGlobPatternStat(TModuleGlobsData& moduleGlobsData, const TCmdElemId globPatternElemId, TGlobStat&& globPatternStat);
    static const TGlobStat& GetGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const TCmdElemId globPatternElemId);
    static TGlobStat LoadGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const TCmdElemId globPatternElemId);
};
