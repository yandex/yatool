#include "ydl_parser.h"
#include "ydl_imports_parser.h"

#include <util/string/subst.h>


void TYDLIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) {
    TStringBuf input = file.GetContent();

    TVector<TString> imports;
    ScanYDLImports(input, imports);

    for (auto&& import: imports) {
        SubstGlobal(import, '.', '/');
        includes.push_back(import + ".ydl");
    }
}
