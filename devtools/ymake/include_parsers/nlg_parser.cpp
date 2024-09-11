#include "nlg_parser.h"
#include "nlg_includes_parser.h"

void TNlgIncludesParser::Parse(IContentHolder& file, TVector<TInclDep>& imports) const {
    TStringBuf input = file.GetContent();
    ScanNlgIncludes(input, imports);
}
