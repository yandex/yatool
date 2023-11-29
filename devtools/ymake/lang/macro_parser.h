#pragma once

#include <devtools/ymake/macro.h>

#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

int GetMacrosFromString(const TStringBuf& content, TVector<TMacroData>& macros, const THashSet<TStringBuf>& ownVars, const TStringBuf& cmdName);
