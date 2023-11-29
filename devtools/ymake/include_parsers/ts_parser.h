#pragma once

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

#include <devtools/ymake/symbols/symbols.h>

class TTsImportParser {
public:
    constexpr static const TStringBuf PARSE_ERROR_PREFIX = "__ymake_parse_error__";
    constexpr static const TStringBuf IGNORE_IMPORT = "__ymake_ignore_import__";

    void Parse(IContentHolder& file, TVector<TString>& imports);
};
