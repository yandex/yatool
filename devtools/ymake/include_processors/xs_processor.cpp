#include "xs_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/induced_props.h>
#include <devtools/ymake/module_wrapper.h>

TXsIncludeProcessor::TXsIncludeProcessor(TSymbols& symbols) {
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "xs"}, TIndDepsRule::EAction::Use));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "xscpp"}, TIndDepsRule::EAction::Pass));
}

void TXsIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                              TModuleWrapper& module,
                                              TFileView incFileName,
                                              const TVector<TInclDep>& includes) const {
    TVector<TString> preparedInduced;
    TVector<TString> preparedIncludes;
    for (const auto& inclDep : includes) {
        if (inclDep.IsInduced) {
            preparedInduced.push_back(inclDep.Path);
        } else {
            preparedIncludes.push_back(inclDep.Path);
        }
    }
    ResolveAndAddLocalIncludes(node, module, incFileName, preparedIncludes, {}, LanguageId);

    TVector<TResolveFile> resolvedInduced(Reserve(preparedInduced.size()));
    module.ResolveAsUnset(preparedInduced, resolvedInduced);
    node.AddParsedIncls("cpp", resolvedInduced);
}

void TXsIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                TModuleWrapper& module,
                                                TFileView incFileName,
                                                const TVector<TString>& includes) const {
    // Treat OUTPUT_INCLUDES as both native & cpp
    ResolveAndAddLocalIncludes(node, module, incFileName, includes, "cpp", LanguageId);
}
