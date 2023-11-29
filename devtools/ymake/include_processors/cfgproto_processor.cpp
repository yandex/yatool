#include "cfgproto_processor.h"

#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/add_dep_adaptor_inline.h>

TCfgprotoIncludeProcessor::TCfgprotoIncludeProcessor(TSymbols& symbols)
    : TProtoIncludeProcessor(symbols)
{
}


void TCfgprotoIncludeProcessor::ProcessHIncludes(TVector<TResolveFile>& hResolved,
                                              TVector<TString>& imports,
                                              TModuleWrapper& module,
                                              TFileView incFileName,
                                              const TVector<TInclDep>& parsedIncludes) const {
    TVector<TString> hIncludes;
    for (const auto& inclDep : parsedIncludes) {
        if (inclDep.IsInduced) {
            hIncludes.push_back(inclDep.Path);
        } else {
            imports.push_back(inclDep.Path);
        }
    }
    module.ResolveLocalIncludes(incFileName, hIncludes, hResolved, LanguageId);
}

void TCfgprotoIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                               TModuleWrapper& module,
                                               TFileView incFileName,
                                               const TVector<TInclDep>& parsedIncludes) const {
    TVector<TResolveFile> hIncludes;
    TVector<TString> imports;
    TVector<TResolveFile> nativeIncludes;
    ProcessHIncludes(hIncludes, imports, module, incFileName, parsedIncludes);
    ParseProtoIncludes(node, module, incFileName, nativeIncludes, hIncludes, imports);
}
