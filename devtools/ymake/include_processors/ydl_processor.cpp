#include "ydl_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>


void TYDLIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                           TModuleWrapper& module,
                                           TFileView incFileName,
                                           const TVector<TString>& includes) const {
    TVector<TString> nativeIncludes;
    TVector<TString> inducedIncludes;

    for (const auto& include: includes) {
        if (include.EndsWith(".ydl")) {
            nativeIncludes.push_back(include);
            inducedIncludes.push_back(include + ".h");
        } else {
            inducedIncludes.push_back(include);
        }
    }

    TVector<TResolveFile> resolvedIncludes;
    module.ResolveLocalIncludes(incFileName, nativeIncludes, resolvedIncludes, LanguageId);

    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, module);
    }

    TVector<TResolveFile> resolvedInduced(Reserve(inducedIncludes.size()));
    module.ResolveAsUnset(inducedIncludes, resolvedInduced);
    node.AddParsedIncls("h+cpp", resolvedInduced);
}
