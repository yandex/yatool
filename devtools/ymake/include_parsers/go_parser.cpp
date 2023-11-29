#include "go_parser.h"
#include "go_import_parser.h"

#include <util/generic/strbuf.h>

void TGoImportParser::Parse(IContentHolder& file, TVector<TParsedFile>& parsedFiles) {
    TStringBuf input = file.GetContent();
    parsedFiles.reserve(32);
    ScanGoImports(input, parsedFiles);
}

