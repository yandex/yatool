#pragma once

#include "include.h"
#include "parsers_cache.h"

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/add_dep_adaptor.h>
#include <devtools/ymake/addincls.h>
#include <devtools/ymake/induced_props.h>

#include <devtools/ymake/include_parsers/incldep.h>

#include <util/generic/bitops.h>

class TModuleWrapper;
class TModuleResolver;

class TIncludeProcessorBase {
protected:
    TIndDepsRule Rule;

public:
    TLangId LanguageId = TModuleIncDirs::C_LANG;

public:
    TIncludeProcessorBase() = default;
    virtual ~TIncludeProcessorBase() = default;

    const TIndDepsRule* DepsTransferRules() const;
    virtual void RegisterIndDepsRule(TSymbols&);
    virtual void ProcessOutputIncludes(TAddDepAdaptor& node,
                                       TModuleWrapper& module,
                                       TFileView incFileName,
                                       const TVector<TString>& includes) const = 0;
    virtual ui32 Version() const { return 0; }

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
    ui32 Version() const override { return 1; }
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

class TParserId {
public:
    using TIdType = ui32;

    explicit TParserId(TIdType id = 0) : Id(id) {
        static_assert(0 < CodeBitsSize, "violated: CodeBitsSize > 0");
        static_assert(CodeBitsSize < sizeof(TIdType) * 8, "violated: CodeBitsSize < sizeof(TIdType) * 8");
    }

    TIdType GetId() const { return Id; }
    void SetCode(ui32 code) { SetBits<0, CodeBitsSize, TIdType>(Id, code); }
    ui32 GetCode() const { return SelectBits<0, CodeBitsSize, TIdType>(Id); }
    void SetVersion(ui32 version) { SetBits<CodeBitsSize, VersionBitsSize, TIdType>(Id, version); }
    ui32 GetVersion() const { return SelectBits<CodeBitsSize, VersionBitsSize, TIdType>(Id); }
private:
    static constexpr size_t CodeBitsSize = 16;
    static constexpr size_t VersionBitsSize = sizeof(TIdType) * 8 - CodeBitsSize;

    ui32 Id;
};

class TParserBase {
public:
    virtual bool ProcessOutputIncludes(TAddDepAdaptor& node,
                                       TModuleWrapper& module,
                                       TFileView incFileName,
                                       const TVector<TString>& includes) const = 0;
    virtual bool ParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) = 0;
    virtual const TIndDepsRule* DepsTransferRules() const = 0;
    virtual void SetLanguageId(ui32 parserCode, TLangId) {
        SetParserCode(parserCode);
    };
    virtual void RegisterIndDepsRule(TSymbols&){};
    virtual ~TParserBase() = default;

    TParserId GetParserId() const { return ParserId; }

protected:
    void SetParserCode(ui32 code) { ParserId.SetCode(code); }
    void SetParserVersion(ui32 version) { ParserId.SetVersion(version); }

private:
    TParserId ParserId;
};

using TParserBaseRef = TSimpleSharedPtr<TParserBase>;
using TParsersList = TVector<std::pair<TParserBaseRef /*parser*/, TVector<TString> /*extensions*/>>;

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
        auto cacheResultId = NParsersCache::GetResultId(GetParserId().GetId(), incFile.GetTargetId());
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

    const TIndDepsRule* DepsTransferRules() const override {
        return IncludeProcessor.DepsTransferRules();
    }

    void SetLanguageId(ui32 parserCode, TLangId processorId) override {
        SetParserCode(parserCode);
        IncludeProcessor.LanguageId = processorId;
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
