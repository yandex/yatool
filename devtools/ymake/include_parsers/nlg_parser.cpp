#include "nlg_parser.h"
#include "nlg_includes_parser.h"

void TNlgIncludesParser::Parse(IContentHolder& file, TVector<TInclDep>& imports) {
    TStringBuf input = file.GetContent();
    ScanNlgIncludes(input, imports);
}
