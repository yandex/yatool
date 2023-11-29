#include "gzt_processor.h"

#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/add_dep_adaptor_inline.h>

TGztIncludeProcessor::TGztIncludeProcessor(TSymbols& symbols)
    : TProtoIncludeProcessor(symbols)
{
}

void TGztIncludeProcessor::GenerateProtoIncludes(TVector<TResolveFile>& protoIncludes,
                                                 const TVector<TResolveFile>& nativeIncludes,
                                                 TModuleWrapper& module) const {
    for (const auto& include : nativeIncludes) {
        auto includeBuf = module.GetTargetBuf(include);
        if (NPath::Extension(includeBuf) == TStringBuf("gztproto")) {
            // FIXME(spreis) This is unreliable and traversal-dependent, so fix ths as soon as better resolution scheme is ready
            auto gztprotoInclude = module.ResolveAsOutput(NPath::CutAllTypes(TString::Join(NPath::NoExtension(includeBuf), TStringBuf(".proto"))));
            protoIncludes.emplace_back(gztprotoInclude);
        } else {
            protoIncludes.emplace_back(include);
        }
    }
}

void TGztIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                           TModuleWrapper& module,
                                           TFileView incFileName,
                                           const TVector<TString>& includes) const {
    TVector<TResolveFile> hIncludes;
    TVector<TResolveFile> nativeIncludes;
    ParseProtoIncludes(node, module, incFileName, nativeIncludes, hIncludes, includes);

    TVector<TResolveFile> protoIncludes;
    GenerateProtoIncludes(protoIncludes, nativeIncludes, module);
    node.AddParsedIncls("proto", protoIncludes);
}
