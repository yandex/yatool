#pragma once

#include "go_parser.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

void ScanGoImports(const TStringBuf input, TVector<TParsedFile>& parsedFiles);
