#pragma once

#include <util/string/vector.h>

class TModule;
void ParseDllModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);
void ParseProgramModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);
void ParseUnitTestModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);
void ParseLibraryModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);
void ParseBaseModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);
void ParseRawModuleArgs(TModule* mod, TArrayRef<const TStringBuf> args);

void SetDirNameBasename(TModule* mod);
void SetDirNameBasenameOrGoPackage(TModule* mod);
void SetTwoDirNamesBasename(TModule* mod);
void SetThreeDirNamesBasename(TModule* mod);
void SetFullPathBasename(TModule* mod);
