#include "cpp_processor.h"

#include <devtools/ymake/module_wrapper.h>

bool TryPrepareCppInclude(const TString& include, TInclude& result) {
    if (include.empty()) {
        return false;
    }

    TString subPath;
    auto IncludeBodyExtracted = [&](TStringBuf prefix, TStringBuf postfix) -> bool {
        const size_t requiredSize = prefix.size() + postfix.size() + 1;
        if (include.size() < requiredSize) {
            return false;
        }
        if (include.StartsWith(prefix) && include.EndsWith(postfix)) {
            subPath = include.substr(prefix.size(), include.size() - prefix.size() - 1);
            return true;
        }
        return false;
    };

    if (IncludeBodyExtracted("<", ">")) {
        result.Path = subPath;
        result.Kind = TInclude::EKind::System;
        return true;
    }

    if (IncludeBodyExtracted("\"", "\"")) {
        result.Path = subPath;
        result.Kind = TInclude::EKind::Local;
        return true;
    }

    if (IncludeBodyExtracted("Y_UCRT_INCLUDE_NEXT(", ")") ||
        IncludeBodyExtracted("Y_MSVC_INCLUDE_NEXT(", ")")) {
        return false;
    }

    result.Path = include;
    result.Kind = TInclude::EKind::Macro;
    return true;
}

void TCppIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                           TModuleWrapper& module,
                                           TFileView incFileName,
                                           const TVector<TString>& includes) const {
    TVector<TInclude> preparedIncludes;
    preparedIncludes.reserve(includes.size());
    for (const auto& include : includes) {
        TInclude preparedInclude;
        if (TryPrepareCppInclude(include, preparedInclude)) {
            preparedIncludes.emplace_back(preparedInclude);
        }
    }

    TVector<TResolveFile> resolvedIncludes;
    resolvedIncludes.reserve(preparedIncludes.size());
    module.ResolveIncludes(incFileName, preparedIncludes, resolvedIncludes, LanguageId);
    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, module);
    }
}


void TCppIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                 TModuleWrapper& module,
                                                 TFileView incFileName,
                                                 const TVector<TString>& includes) const {
    ResolveAndAddLocalIncludes(node, module, incFileName, includes, {}, LanguageId);
}

TCLikeIncludeProcessor::TCLikeIncludeProcessor(TSymbols& symbols) {
    Rule = TIndDepsRule({TPropertyType{symbols, EVI_InducedDeps, "cpp"},
                         TPropertyType{symbols, EVI_InducedDeps, "h+cpp"},
                         TPropertyType{symbols, EVI_InducedDeps, "xscpp"}});
}

TCHeaderIncludeProcessor::TCHeaderIncludeProcessor(TSymbols& symbols) {
    Rule = TIndDepsRule({TPropertyType{symbols, EVI_InducedDeps, "h"},
                         TPropertyType{symbols, EVI_InducedDeps, "h+cpp"}});
}
