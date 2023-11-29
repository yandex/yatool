#include "proto_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/ymake.h>

static const TVector<TStringBuf>& InducedHeaderExts(const TStringBuf& name, const TModuleWrapper& module, TVector<TStringBuf>& exts) {
    TStringBuf varName;
    TStringBuf defExt;
    if (NPath::Extension(name) == TStringBuf("ev")) {
        varName = TStringBuf("EV_HEADER_EXTS");
        defExt = TStringBuf(".ev.pb.h");
    } else if (NPath::Extension(name) == TStringBuf("cfgproto")) {
        varName = TStringBuf("CFGPROTO_HEADER_EXTS");
        defExt = TStringBuf(".cfgproto.pb.h");
    } else {
        varName = TStringBuf("PROTO_HEADER_EXTS");
        defExt = TStringBuf(".pb.h");
    }
    TStringBuf extsValue = module.Get(varName);
    exts.clear();
    if (extsValue.size()) {
        Split(extsValue, " ", exts);
    } else {
        exts.emplace_back(defExt);
    }
    return exts;
}

static TString ApplyHeader(const TStringBuf& name, const TStringBuf& ext) {
    return TString::Join(NPath::NoExtension(name), ext);
}

TProtoIncludeProcessor::TProtoIncludeProcessor(TSymbols& symbols) {
    Rule.Actions.clear();
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "proto"}, TIndDepsRule::EAction::Use));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "h"}, TIndDepsRule::EAction::Pass));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "cpp"}, TIndDepsRule::EAction::Pass));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "h+cpp"}, TIndDepsRule::EAction::Pass));

}

void TProtoIncludeProcessor::PrepareIncludes(TVector<TResolveFile>& nativeResolved,
                                             TVector<TResolveFile>& hResolved,
                                             TModuleWrapper& module,
                                             TFileView incFileName,
                                             const TVector<TString>& parsedIncludes) const {
    module.ResolveLocalIncludes(incFileName, parsedIncludes, nativeResolved, LanguageId);
    TVector<TStringBuf> exts;
    for (const auto& include : parsedIncludes) {
        for (const auto& ext : InducedHeaderExts(include, module, exts)) {
            TString header = ApplyHeader(include, ext);
            hResolved.emplace_back(module.MakeUnresolved(NPath::CutAllTypes(header)));
        }
    }
}

void TProtoIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                             TModuleWrapper& module,
                                             TFileView incFileName,
                                             const TVector<TString>& parsedIncludes) const {
    TVector<TResolveFile> hIncludes;
    TVector<TResolveFile> nativeIncludes;
    ParseProtoIncludes(node, module, incFileName, nativeIncludes, hIncludes, parsedIncludes);
}


void TProtoIncludeProcessor::ParseProtoIncludes(TAddDepAdaptor& node,
                                                TModuleWrapper& module,
                                                TFileView incFileName,
                                                TVector<TResolveFile>& nativeIncludes,
                                                TVector<TResolveFile>& hIncludes,
                                                const TVector<TString>& parsedIncludes) const {
    PrepareIncludes(nativeIncludes, hIncludes, module, incFileName, parsedIncludes);

    if (!nativeIncludes.empty()) {
        AddIncludesToNode(node, nativeIncludes, module);
    }

    node.AddParsedIncls("h+cpp", hIncludes);
}
