#pragma once

#include <devtools/ymake/symbols/globs.h>

class TModule;

class TGlobHelper {
public:
    static TGlobRestrictions ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro);

    static void SaveGlobPatternElemIds(TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId, TVector<ui32>&& globPatternElemIds);
    static const TVector<ui32>& GetGlobPatternElemIds(const TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId);

    static void SaveGlobRestrictions(TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId, TGlobRestrictions&& globRestrictions);
    static const TGlobRestrictions& GetGlobRestrictions(const TModuleGlobsData& moduleGlobsData, const ui32 globVarElemId);

    static void SaveGlobPatternStat(TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId, TGlobStat&& globPatternStat);
    static const TGlobStat& GetGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId);
    static TGlobStat LoadGlobPatternStat(const TModuleGlobsData& moduleGlobsData, const ui32 globPatternElemId);
};
