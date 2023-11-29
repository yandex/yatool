#include "swig_processor.h"

#include "cpp_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

#include <util/string/split.h>

TSwigIncludeProcessor::TSwigIncludeProcessor() {
    Rule.PassInducedIncludesThroughFiles = true;
}

// AddImplicitIncludes adds implicit %includes from swig/Source/Modules/main.cxx.
static void AddImplicitIncludes(TModuleWrapper& module, TVector<TInclude>& includes) {
    TStringBuf roots = module.GetModule().Vars.EvalValue("SWIG_IMPLICIT_INCLUDES");
    for (TStringBuf root : StringSplitter(roots).Split(' ').SkipEmpty()) {
        includes.emplace_back(TInclude::EKind::System, root);
    }
}

void TSwigIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                            TModuleWrapper& module,
                                            TFileView incFileName,
                                            const TVector<TInclDep>& inclDeps) const {
    TVector<TInclude> includes, induced;

    // We'd rather not add implicit includes to %include files, so we exclude
    // non-.swg files and the swig library.
    TStringBuf incFileNameStr = incFileName.GetTargetStr();
    if (incFileNameStr.EndsWith(".swg") && !incFileNameStr.Contains("/swig/Lib/") && !incFileNameStr.Contains("/swiglib/")) {
        AddImplicitIncludes(module, includes);
    }

    for (const auto& inclDep : inclDeps) {
        TInclude include;
        if (!TryPrepareCppInclude(inclDep.Path, include)) {
            YConfErr(BadIncl) << "failed to parse swig include " << inclDep.Path << Endl;
            include.Path = inclDep.Path;
        }
        if (inclDep.IsInduced) {
            induced.push_back(include);
        } else {
            includes.push_back(include);
        }
    }

    TVector<TResolveFile> resolved;
    module.ResolveIncludes(incFileName, includes, resolved, LanguageId);
    if (resolved) {
        AddIncludesToNode(node, resolved, module);
    }

    resolved.clear();
    module.ResolveAsUnset(induced, resolved);
    node.AddParsedIncls("h+cpp", resolved);
}

void TSwigIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                  TModuleWrapper& module,
                                                  TFileView outputFileName,
                                                  const TVector<TString>& outputIncludes) const {
    TVector<TInclude> includes;
    TVector<TResolveFile> resolved;

    AddImplicitIncludes(module, includes);
    module.ResolveIncludes(outputFileName, includes, resolved, LanguageId);
    if (resolved) {
        AddIncludesToNode(node, resolved, module);
    }

    resolved.clear();
    module.ResolveLocalIncludes(outputFileName, outputIncludes, resolved, LanguageId);
    if (resolved) {
        //TODO(kikht): maybe we should split includes based on their ext
        AddIncludesToNode(node, resolved, module);
    }

    resolved.clear();
    module.ResolveAsUnset(outputIncludes, resolved);
    node.AddParsedIncls("h+cpp", resolved);
}
