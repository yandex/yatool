#include "nlg_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>


namespace {

TVector<TInclude> PrepareIncludes(const TVector<TInclDep>& inclDeps,
                                  const TString& ext) {
    TVector<TInclude> preparedIncludes;
    for (const auto& inclDep : inclDeps) {
        if (!inclDep.IsInduced && ext == ".pb.txt") {
            continue;
        }
        TInclude preparedInclude;
        preparedInclude.Path = inclDep.Path + ext;
        preparedInclude.Kind = TInclude::EKind::System;
        preparedIncludes.push_back(preparedInclude);
    }

    return std::move(preparedIncludes);
}

} // namespace

TNlgIncludeProcessor::TNlgIncludeProcessor() {
    Rule.PassInducedIncludesThroughFiles = true;
}

void TNlgIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                           TModuleWrapper& moduleWrapper,
                                           TFileView incFileName,
                                           const TVector<TInclDep>& inclDeps) const {
    TVector<TResolveFile> resolvedIncludes;
    TVector<TInclude> protoIncludes = PrepareIncludes(inclDeps, ".pb.txt");
    moduleWrapper.ResolveIncludes(incFileName, protoIncludes, resolvedIncludes);
    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, moduleWrapper);
    }

    TVector<TInclude> headerIncludes = PrepareIncludes(inclDeps, ".h");
    resolvedIncludes.clear();
    moduleWrapper.ResolveAsUnset(headerIncludes, resolvedIncludes);
    node.AddParsedIncls("h+cpp", resolvedIncludes);
}

void TNlgIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                 TModuleWrapper& moduleWrapper,
                                                 TFileView /* incFileName */,
                                                 const TVector<TString>& includes) const {
    TVector<TInclude> preparedIncludes;
    TVector<TResolveFile> resolvedIncludes;
    for (const auto& include : includes) {
        TInclude preparedInclude;
        preparedInclude.Path = include + ".h";
        preparedInclude.Kind = TInclude::EKind::System;
        preparedIncludes.push_back(preparedInclude);
    }

    moduleWrapper.ResolveAsUnset(preparedIncludes, resolvedIncludes);
    node.AddParsedIncls("h+cpp", resolvedIncludes);
}
