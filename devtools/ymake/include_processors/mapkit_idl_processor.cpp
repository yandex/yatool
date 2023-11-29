#include "mapkit_idl_processor.h"

#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/parser_manager.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

TMapkitIdlIncludeProcessor::TMapkitIdlIncludeProcessor(TSymbols& symbols) {
    Rule.Actions.clear();
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "idl"}, TIndDepsRule::EAction::Use));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "mapkitidl"}, TIndDepsRule::EAction::Use));

    LanguageProto = NLanguages::GetLanguageId("proto"sv);
    Y_ABORT_UNLESS(LanguageProto != NLanguages::BAD_LANGUAGE);
}

void TMapkitIdlIncludeProcessor::ProcessIncludes(
    TAddDepAdaptor& node,
    TModuleWrapper& module,
    TFileView incFileName,
    const TVector<TString>& includes
) const {
    TVector<TString> nativeIncludes;
    TVector<TString> protoIncludes;
    for (const auto& inc : includes) {
        if (inc.EndsWith(".proto"sv)) {
            protoIncludes.push_back(inc);
        } else {
            nativeIncludes.push_back(inc);
        }
    }
    ResolveAndAddLocalIncludes(node, module, incFileName, nativeIncludes, {}, LanguageId);
    ResolveAndAddLocalIncludes(node, module, incFileName, protoIncludes, {}, LanguageProto);
}
