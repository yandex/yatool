#include "ts_parser.h"
#include "ts_import_parser.h"

#include <util/generic/strbuf.h>

void TTsImportParser::Parse(IContentHolder& file, TVector<TString>& imports) const {
    TStringBuf input = file.GetContent();
    // ~14 imports per file on q99 (dirty /frontend stats).
    imports.reserve(16);
    ScanTsImports(input, imports);
}
