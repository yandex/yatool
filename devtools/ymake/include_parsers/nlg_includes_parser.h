#pragma once

#include "nlg_parser.h"
#include "incldep.h"

#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

void ScanNlgIncludes(const TStringBuf input, TVector<TInclDep>& includes);
