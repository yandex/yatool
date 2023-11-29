#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

void ScanYDLImports(const TStringBuf input, TVector<TString>& imports);
