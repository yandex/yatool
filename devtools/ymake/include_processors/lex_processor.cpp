#include "lex_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

void TLexIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                           TModuleWrapper& module,
                                           TFileView /* incFileName */,
                                           const TVector<TString>& includes) const {
    TVector<TResolveFile> resolvedIncludes(Reserve(includes.size()));
    module.ResolveAsUnset(includes, resolvedIncludes);
    node.AddParsedIncls("h+cpp", resolvedIncludes);
}
