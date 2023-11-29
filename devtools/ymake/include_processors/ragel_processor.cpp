#include "ragel_processor.h"
#include "cpp_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

void TRagelIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                             TModuleWrapper& module,
                                             TFileView incFileName,
                                             const TVector<TRagelInclude>& includes) const {
    TVector<TResolveFile> cppIncludes;
    TVector<TResolveFile> nativeIncludes;
    PrepareIncludes(nativeIncludes, cppIncludes, module, incFileName, includes);
    if (!nativeIncludes.empty()) {
        AddIncludesToNode(node, nativeIncludes, module);
    }
    node.AddParsedIncls("h+cpp", cppIncludes);
}

void TRagelIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                   TModuleWrapper& module,
                                                   TFileView incFileName,
                                                   const TVector<TString>& includes) const {
    // Treat OUTPUT_INCLUDES as both native & cpp
    TVector<TResolveFile> resolved;
    module.ResolveLocalIncludes(incFileName, includes, resolved, LanguageId);
    if (!resolved.empty()) {
        AddIncludesToNode(node, resolved, module);
    }

    TVector<TResolveFile> resolvedInduced(Reserve(includes.size()));
    module.ResolveAsUnset(includes, resolvedInduced);
    node.AddParsedIncls("h+cpp", resolvedInduced);
}

void TRagelIncludeProcessor::PrepareIncludes(TVector<TResolveFile>& nativeResolved,
                                             TVector<TResolveFile>& cppResolved,
                                             TModuleWrapper& module,
                                             TFileView incFileName,
                                             const TVector<TRagelInclude>& includes) const {
    TVector<TInclude> cppIncludes;
    TVector<TInclude> nativeIncludes;
    ScanIncludes(nativeIncludes, cppIncludes, includes);
    if (!cppIncludes.empty()) {
        // We do not resolve ParsedIncls
        module.ResolveAsUnset(cppIncludes, cppResolved);
    }

    if (!nativeIncludes.empty()) {
        module.ResolveIncludes(incFileName, nativeIncludes, nativeResolved, LanguageId);
    }
}

void TRagelIncludeProcessor::ScanIncludes(TVector<TInclude>& nativeIncludes,
                                          TVector<TInclude>& cppIncludes,
                                          const TVector<TRagelInclude>& includes) const {
    for (const auto& include : includes) {
        if (include.Kind == TRagelInclude::EKind::Native) {
            nativeIncludes.push_back(TInclude(TInclude::EKind::Local, include.Include));
        } else if (include.Kind == TRagelInclude::EKind::Cpp) {
            TInclude inc;
            if (TryPrepareCppInclude(include.Include, inc)) {
                cppIncludes.push_back(inc);
            }
        }
    }
}
