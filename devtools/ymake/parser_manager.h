#pragma once

#include <devtools/ymake/conf.h>

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/include_processors/base.h>
#include <devtools/ymake/include_processors/include.h>
#include <devtools/ymake/module_resolver.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/singleton.h>
#include <util/generic/strbuf.h>
#include <util/generic/hash.h>
#include <util/generic/set.h>

class TYMake;

// NLanguages provides access to the list of languages.
//
// A language has its own ADDINCLs, list of extensions, parser and processor.
// It is defined by the name or id (sequential number of the language).
//
// Each known extension has a parser for processing. Parser's id is its sequential number.
// Language can be unambiguous determined by extension or parser (the inverse is false).
namespace NLanguages {
    const constexpr TLangId BAD_LANGUAGE = static_cast<TLangId>(std::numeric_limits<ui32>::max());

    TLangId GetLanguageId(const TStringBuf& name);

    TLangId GetLanguageIdByExt(const TStringBuf& ext);

    TLangId GetLanguageIdByParserType(EIncludesParserType type);

    TStringBuf GetLanguageName(TLangId languageId);

    const TString& GetLanguageIncludeName(TLangId languageId);

    bool GetLanguageAddinclsAreNonPaths(TLangId languageId);

    TString DumpLanguagesList();

    size_t LanguagesCount();
}

struct TFileProcessContext {
    const TBuildConfiguration& Conf;
    TModuleResolveContext ModuleResolveContext;
    const TAddIterStack& Stack;
    TModule& Module;
    TAddDepAdaptor& Node;
};

class TIncParserManager {
private:
    THashMap<TString, TParserBaseRef> Ext2Parser;
    TVector<TParserBaseRef> ParsersByType;
    const TBuildConfiguration& Conf;
    TSymbols& Names;
    TString ExtForDefaultParser;

public:
    TParsersCache Cache;
    mutable NStats::TIncParserManagerStats Stats{"Parsing stats"};

public:
    explicit TIncParserManager(const TBuildConfiguration& conf, TSymbols& names);
    void InitManager(const TParsersList& parsersList); // must be called after loading graph from cache (uses id's for Graph)
    void AddParsers(const TParsersList& parsersList);

    void ProcessFile(TFileContentHolder& incFile, TFileProcessContext context) const;
    bool HasIncludeChanges(TFileContentHolder& incFile, const TParserBase* parser) const;
    void ProcessFileWithSubst(TFileContentHolder& incFile, TFileProcessContext context) const;
    bool ProcessOutputIncludes(TFileView outputFileName,
                               const TVector<TString>& includes,
                               TModuleWrapper& module,
                               TAddDepAdaptor& node,
                               const TSymbols& names,
                               const TAddIterStack& stack) const;
    void AddParser(TParserBaseRef parser, const TVector<TString>& extensions, EIncludesParserType type);
    bool HasParserFor(TStringBuf fileName) const;
    bool HasParserFor(TFileView fileName) const;
    TParserBase* GetParserFor(TStringBuf fileName) const;
    TParserBase* GetParserFor(TFileView fileName) const;
    inline TParserBase* GetParserByType(EIncludesParserType parserType) const {
        Y_ASSERT(parserType < EIncludesParserType::PARSERS_COUNT);
        return ParsersByType[static_cast<ui32>(parserType)].Get();
    }
    void SetDefaultParserSameAsFor(TFileView fileName);
    void ResetDefaultParser() {
        ExtForDefaultParser.clear();
    }

    const TIndDepsRule* IndDepsRuleByPath(const TStringBuf& path) const;
    const TIndDepsRule* IndDepsRuleByPath(TFileView path) const;
    const TIndDepsRule* IndDepsRuleByExt(const TStringBuf& path) const;

private:
    TParserBase* ParserByExt(const TStringBuf& ext) const;
    TStringBuf ExtPreprocess(TStringBuf ext,
                             const TSymbols& names,
                             const TAddIterStack& stack) const;
};
