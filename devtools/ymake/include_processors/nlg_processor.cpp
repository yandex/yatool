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

    // We actually need "include" dependencies between *.pb.txt files.
    // The *.pb.txt files act as representatives of corresponding *.nlg "modules"
    // and should be available transitively when generating code for subsequent
    // dependent *.nlg-s.
    //
    // Links between dummy.h files are not enough, as they are not direct inputs (or their "include"-s)
    // for code generation, and we should not form transitive closure interchanging "include"-s
    // between main and additional outputs.
    //
    // We also can not add "include" edge directly to the additional output (which is *.pb.txt),
    // and can not bind induced dependencies to "txt" extension as there is no processor and rule
    // for this extension.
    // So we binds *.pb.txt to everything, and actually as all the files are the outputs of a single command
    // no real excessive dependencies will be created in JSON graph.
    resolvedIncludes.clear();
    moduleWrapper.ResolveAsUnset(protoIncludes, resolvedIncludes);
    node.AddParsedIncls("*", resolvedIncludes);

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
