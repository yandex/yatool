#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

TString ResolveIncludePath(const TStringBuf& path, const TStringBuf& fromFile);
