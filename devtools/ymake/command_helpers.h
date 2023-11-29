#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

TVector<TVector<TString>> SplitCommandsAndArgs(const TString& cmd);
TVector<TString> SplitCommands(const TString& cmd);
TVector<TString> SplitArgs(const TString& cmd);
