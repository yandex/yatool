#include "base.h"

#include "cfgproto_processor.h"
#include "cpp_processor.h"
#include "cython_processor.h"
#include "flatc_processor.h"
#include "fortran_processor.h"
#include "gzt_processor.h"
#include "go_processor.h"
#include "lex_processor.h"
#include "mapkit_idl_processor.h"
#include "nlg_processor.h"
#include "proto_processor.h"
#include "ragel_processor.h"
#include "ros_processor.h"
#include "swig_processor.h"
#include "xs_processor.h"
#include "ydl_processor.h"
#include "ts_processor.h"

#include <devtools/ymake/include_parsers/cfgproto_parser.h>
#include <devtools/ymake/include_parsers/cpp_parser.h>
#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/include_parsers/flatc_parser.h>
#include <devtools/ymake/include_parsers/fortran_parser.h>
#include <devtools/ymake/include_parsers/go_parser.h>
#include <devtools/ymake/include_parsers/lex_parser.h>
#include <devtools/ymake/include_parsers/mapkit_idl_parser.h>
#include <devtools/ymake/include_parsers/nlg_parser.h>
#include <devtools/ymake/include_parsers/proto_parser.h>
#include <devtools/ymake/include_parsers/ragel_parser.h>
#include <devtools/ymake/include_parsers/ros_parser.h>
#include <devtools/ymake/include_parsers/ros_topic_parser.h>
#include <devtools/ymake/include_parsers/sc_parser.h>
#include <devtools/ymake/include_parsers/swig_parser.h>
#include <devtools/ymake/include_parsers/xs_parser.h>
#include <devtools/ymake/include_parsers/xsyn_parser.h>
#include <devtools/ymake/include_parsers/ydl_parser.h>
#include <devtools/ymake/include_parsers/ts_parser.h>

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

const TIndDepsRule* TIncludeProcessorBase::DepsTransferRules() const {
    return &Rule;
}

void TIncludeProcessorBase::RegisterIndDepsRule(TSymbols&) {
    // No operations.
}

void TStringIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                    TModuleWrapper& module,
                                                    TFileView incFileName,
                                                    const TVector<TString>& includes) const {
    ProcessIncludes(node, module, incFileName, includes);
}

TEmptyIncludeProcessor::TEmptyIncludeProcessor(bool passNoInducedDeps) {
    Rule.PassNoInducedDeps = passNoInducedDeps;
}


void TEmptyIncludeProcessor::ProcessIncludes(TAddDepAdaptor&,
                                             TModuleWrapper&,
                                             TFileView,
                                             const TVector<TString>&) const {
    // No operations.
}


void TNoInducedIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                                 TModuleWrapper& module,
                                                 TFileView incFileName,
                                                 const TVector<TString>& includes) const {
    ResolveAndAddLocalIncludes(node, module, incFileName, includes, {}, LanguageId);
}

void AddIncludesToNode(TAddDepAdaptor& node, TVector<TString>& includes) {
    for (const auto& include : includes) {
        YDIAG(DG) << "Incl dep: " << include << Endl;
        node.AddUniqueDep(EDT_Include, FileTypeByRoot(include), include);
    }
}

void AddIncludesToNode(TAddDepAdaptor& node, TVector<TResolveFile>& includes, TModuleWrapper& module) {
    for (const auto& include : includes) {
        YDIAG(DG) << "Incl dep: " << TResolveFileOut(module, include) << Endl;
        node.AddUniqueDep(EDT_Include, FileTypeByRoot(include.Root()), include.GetElemId());
    }
}

void ResolveAndAddLocalIncludes(TAddDepAdaptor& node,
                                TModuleWrapper& module,
                                TFileView incFileName,
                                const TVector<TString>& includes,
                                TStringBuf parsedIncls,
                                TLangId languageId) {
    TVector<TResolveFile> resolvedIncludes;
    module.ResolveLocalIncludes(incFileName, includes, resolvedIncludes, languageId);
    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, module);
    }
    if (parsedIncls) {
        resolvedIncludes.clear();
        module.ResolveAsUnset(includes, resolvedIncludes);
        node.AddParsedIncls(parsedIncls, resolvedIncludes);
    }
}

TParserBaseRef MakeEmptyParser(TParsersCache* cache, bool passNoInducedDeps) {
    return new TIncludesProcessor<TString, TEmptyIncludesParser, TEmptyIncludeProcessor>(cache, passNoInducedDeps);
}

TParserBaseRef MakeCppParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TCppOnlyIncludesParser, TCppIncludeProcessor>(cache);
}

TParserBaseRef MakeCLikeParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TString, TCppOnlyIncludesParser, TCLikeIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeCHeaderParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TString, TCppOnlyIncludesParser, TCHeaderIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeAsmParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TAsmIncludesParser, TNoInducedIncludeProcessor>(cache);
}

TParserBaseRef MakeProtoParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TString, TProtoIncludesParser, TProtoIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeGztParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TString, TProtoIncludesParser, TGztIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeLexParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TLexIncludesParser, TLexIncludeProcessor>(cache);
}

TParserBaseRef MakeRagelParser(TParsersCache* cache) {
    return new TIncludesProcessor<TRagelInclude, TRagelIncludesParser, TRagelIncludeProcessor>(cache);
}

TParserBaseRef MakeMapkitIdlParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TString, TMapkitIdlIncludesParser, TMapkitIdlIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeFortranParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TFortranIncludesParser, TFortranIncludeProcessor>(cache);
}

TParserBaseRef MakeXsParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TInclDep, TXsIncludesParser, TXsIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeXsynParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TXsynIncludesParser, TNoInducedIncludeProcessor>(cache);
}

TParserBaseRef MakeSwigParser(TParsersCache* cache) {
    return new TIncludesProcessor<TInclDep, TSwigIncludesParser, TSwigIncludeProcessor>(cache);
}

TParserBaseRef MakeCythonParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TCythonDep, TCythonIncludesParser, TCythonIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeFlatcParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TFlatcIncludesParser, TFlatcIncludeProcessor>(cache);
}

TParserBaseRef MakeFlatcParser64(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TFlatcIncludesParser, TFlatcIncludeProcessor64>(cache);
}

TParserBaseRef MakeGoParser(TParsersCache* cache, const TEvaluatorBase& evaluator, TSymbols& symbols) {
    return new TIncludesProcessor<TParsedFile, TGoImportParser, TGoImportProcessor>(cache, evaluator, symbols);
}

TParserBaseRef MakeScParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TScIncludesParser, TNoInducedIncludeProcessor>(cache);
}

TParserBaseRef MakeYDLParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TYDLIncludesParser, TYDLIncludeProcessor>(cache);
}

TParserBaseRef MakeNlgParser(TParsersCache* cache) {
    return new TIncludesProcessor<TInclDep, TNlgIncludesParser, TNlgIncludeProcessor>(cache);
}

TParserBaseRef MakeCfgprotoParser(TParsersCache* cache, TSymbols& symbols) {
    return new TIncludesProcessor<TInclDep, TCfgprotoIncludesParser, TCfgprotoIncludeProcessor>(cache, symbols);
}

TParserBaseRef MakeTsParser(TParsersCache* cache) {
    return new TIncludesProcessor<TString, TTsImportParser, TTsImportProcessor>(cache);
}

TParserBaseRef MakeRosParser(TParsersCache* cache) {
    return new TIncludesProcessor<TRosDep, TRosIncludeParser, TRosIncludeProcessor>(cache);
}

TParserBaseRef MakeRosTopicParser(TParsersCache* cache) {
    return new TIncludesProcessor<TRosDep, TRosTopicIncludeParser, TRosIncludeProcessor>(cache);
}
