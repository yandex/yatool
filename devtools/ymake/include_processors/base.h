#pragma once

#include "include.h"
#include "parser_id.h"
#include "parsers_cache.h"

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/add_dep_adaptor.h>
#include <devtools/ymake/addincls.h>
#include <devtools/ymake/induced_props.h>

#include <devtools/ymake/include_parsers/incldep.h>

class TModuleWrapper;
class TModuleResolver;

class TIncludeProcessorBase {
protected:
    TIndDepsRule Rule;
    const ui64 CommonVersion = 1;
public:
    TLangId LanguageId = TModuleIncDirs::C_LANG;

public:
    TIncludeProcessorBase() noexcept = default;
    virtual ~TIncludeProcessorBase() noexcept = default;

    virtual void ProcessOutputIncludes(TAddDepAdaptor& node,
                                       TModuleWrapper& module,
                                       TFileView incFileName,
                                       const TVector<TString>& includes) const = 0;
    virtual ui32 Version() const { return CommonVersion; }
    const TIndDepsRule& DepsTransferRules() const noexcept { return Rule; }

protected:
    template<class TIncl>
    static void ResolveAsUnset(const TVector<TIncl>& includes, TVector<TString>& resolved) {
        resolved.reserve(resolved.size() + includes.size());
        for (const auto& include : includes) {
            TStringBuf name;
            if constexpr(std::is_same<TIncl, TInclude>()) {
                name = include.Path;
            } else {
                name = include;
            }
            if (NPath::IsTypedPath(name)) {
                resolved.emplace_back(name); // use TStringBuf directly
            } else {
                // use constructed TString with $U/
                resolved.emplace_back(NPath::ConstructPath(name, NPath::Unset));
            }
        }
    }
};

class TStringIncludeProcessor: public TIncludeProcessorBase {
public:
    virtual void ProcessIncludes(TAddDepAdaptor& node,
                                 TModuleWrapper& module,
                                 TFileView incFileName,
                                 const TVector<TString>& includes) const = 0;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;
};

class TEmptyIncludeProcessor: public TStringIncludeProcessor {
public:
    explicit TEmptyIncludeProcessor(bool passNoInducedDeps);
    ui32 Version() const override { return 1 + CommonVersion; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;
};

class TNoInducedIncludeProcessor: public TStringIncludeProcessor {
public:
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const;
};

class TParserBase {
public:
    virtual bool ProcessOutputIncludes(TAddDepAdaptor& node,
                                       TModuleWrapper& module,
                                       TFileView incFileName,
                                       const TVector<TString>& includes) const = 0;
    virtual TVector<TString> MapProps(TSymbols&, TPropertyType, const TVector<TStringBuf>& props[[maybe_unused]]) const {return {};}
    virtual bool ParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) = 0;
    virtual bool HasIncludeChanges(TFileContentHolder& incFile) const = 0;
    virtual const TIndDepsRule& DepsTransferRules() const = 0;
    virtual void SetLanguageId(TLangId) {}
    virtual void SetParserType(EIncludesParserType) {}
    virtual ~TParserBase() noexcept = default;

    TParserId GetParserId() const { return ParserId; }

protected:
    void SetType(EIncludesParserType type) { ParserId.SetType(type); }
    void SetParserVersion(ui32 version) { ParserId.SetVersion(version); }

private:
    TParserId ParserId;
};

// Interface for user provided parsers implemented in build configuration rather than in
// tho core part of the build system.
// User provided parsers has some limitations and require different setup in the parser
// creation pipeline. This intermediate abstract class extends base interface with necessary
// customization points.
class TUserParserBase: public TParserBase {
public:
    // limitation enforcements by base parser iface methods final implementations
    bool ProcessOutputIncludes(TAddDepAdaptor&, TModuleWrapper&, TFileView incFileName, const TVector<TString>&) const final;
    void SetLanguageId(TLangId) final {}
    void SetParserType(EIncludesParserType) final {}

    bool ParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) final {
        auto res = true;
        if (BaseParser_)
            res = res && BaseParser_->ParseIncludes(node, module, incFile);
        res = res && DoParseIncludes(node, module, incFile);
        return res;
    }

    bool HasIncludeChanges(TFileContentHolder& incFile) const final {
        auto res = false;
        if (BaseParser_)
            res = res || BaseParser_->HasIncludeChanges(incFile);
        res = res || DoHasIncludeChanges(incFile);
        return res;
    }

    // User parser iface extensions
    virtual void RegisterIndDepsRule(TSymbols&) = 0;
    void SetBaseParser(TParserBase* base) {BaseParser_ = base;}
    const TParserBase* BaseParser() const noexcept {return BaseParser_;}

protected:
    virtual bool DoParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) = 0;
    virtual bool DoHasIncludeChanges(TFileContentHolder& incFile) const = 0;

private:
    TParserBase* BaseParser_ = nullptr;
};

using TParserBaseRef = TSimpleSharedPtr<TParserBase>;
using TUserParserBaseRef = TSimpleSharedPtr<TUserParserBase>;
using TUserParsersList = TVector<std::pair<TUserParserBaseRef /*parser*/, TVector<TString> /*extensions*/>>;

class TEvaluatorBase {
public:
    TEvaluatorBase() = default;
    virtual ~TEvaluatorBase() = default;
    virtual TString EvalVarValue(TStringBuf varName) const = 0;
};

template <class TInclude, class TParser, class TIncludeProcessor>
class TIncludesProcessor: public TParserBase {
public:
    template <typename ...Args>
    TIncludesProcessor(TParsersCache* cache, Args&&... args)
        : ParsersCache(cache)
        , IncludeProcessor(std::forward<Args>(args)...)
    {
        SetParserVersion(IncludeProcessor.Version());
    }

    bool ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override {
        IncludeProcessor.ProcessOutputIncludes(node, module, incFileName, includes);
        return true;
    }

    bool ParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) override {
        TVector<TInclude> includes;
        auto cacheResultId = NParsersCache::GetResultId(GetParserId(), incFile.GetTargetId());
        auto useCachedResult = ParsersCache && !incFile.CheckForChanges(ECheckForChangesMethod::PRECISE);
        if (useCachedResult) {
            useCachedResult = ParsersCache->Get(cacheResultId, includes);
            YDIAG(VV) << "Parsers cache " << (useCachedResult ? "hit" : "miss") << " for " << incFile.GetName() << Endl;
        }
        if (!useCachedResult) {
            Parser.Parse(incFile, includes);
            if (ParsersCache) {
                ParsersCache->Add(cacheResultId, includes);
            }
        }
        IncludeProcessor.ProcessIncludes(node, module, incFile.GetName(), includes);
        return !useCachedResult;
    }

    bool HasIncludeChanges(TFileContentHolder& incFile) const override {
        TVector<TInclude> cachedIncludes;
        auto cacheResultId = NParsersCache::GetResultId(GetParserId(), incFile.GetTargetId());
        if (!ParsersCache) {
            return true;
        }

        bool hasCachedResult = ParsersCache->Get(cacheResultId, cachedIncludes);
        if (!hasCachedResult) {
            return false;
        }

        TVector<TInclude> currentIncludes;
        Parser.Parse(incFile, currentIncludes);

        // считаем, что порядок инклюдов важен, так как влияет на структуру графа,
        // поэтому здесь достаточно просто поэлементного сравнения
        if (currentIncludes != cachedIncludes) {
            return true;
        }

        return false;
    }

    const TIndDepsRule& DepsTransferRules() const override {
        return IncludeProcessor.DepsTransferRules();
    }

    void SetLanguageId(TLangId processorId) override {
        IncludeProcessor.LanguageId = processorId;
    }

    void SetParserType(EIncludesParserType type) override {
        SetType(type);
    }
private:
    TParser Parser;
    TParsersCache* ParsersCache = nullptr;
    TIncludeProcessor IncludeProcessor;
};

void AddIncludesToNode(TAddDepAdaptor& node, TVector<TString>& includes);
void AddIncludesToNode(TAddDepAdaptor& node, TVector<TResolveFile>& includes, TModuleWrapper& module);
void ResolveAndAddLocalIncludes(TAddDepAdaptor& node,
                                TModuleWrapper& module,
                                TFileView incFileName,
                                const TVector<TString>& includes,
                                TStringBuf parsedIncls = {},
                                TLangId languageId = TModuleIncDirs::C_LANG);

TParserBaseRef MakeEmptyParser(TParsersCache* cache, bool passNoInducedDeps);
TParserBaseRef MakeMapkitIdlParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeCHeaderParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeCLikeParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeProtoParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeGztParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeRagelParser(TParsersCache* cache);
TParserBaseRef MakeLexParser(TParsersCache* cache);
TParserBaseRef MakeCppParser(TParsersCache* cache);
TParserBaseRef MakeAsmParser(TParsersCache* cache);
TParserBaseRef MakeFortranParser(TParsersCache* cache);
TParserBaseRef MakeSwigParser(TParsersCache* cache);
TParserBaseRef MakeXsParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeXsynParser(TParsersCache* cache);
TParserBaseRef MakeCythonParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeFlatcParser(TParsersCache* cache);
TParserBaseRef MakeFlatcParser64(TParsersCache*);
TParserBaseRef MakeGoParser(TParsersCache* cache, const TEvaluatorBase& evaluator, TSymbols& symbols);
TParserBaseRef MakeScParser(TParsersCache* cache);
TParserBaseRef MakeYDLParser(TParsersCache* cache);
TParserBaseRef MakeNlgParser(TParsersCache* cache);
TParserBaseRef MakeCfgprotoParser(TParsersCache* cache, TSymbols& symbols);
TParserBaseRef MakeTsParser(TParsersCache* cache);
TParserBaseRef MakeRosParser(TParsersCache* cache);
TParserBaseRef MakeRosTopicParser(TParsersCache* cache);
