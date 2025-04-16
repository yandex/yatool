#include "parser_manager.h"

#include "ymake.h"
#include "makefile_loader.h"
#include "general_parser.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/add_dep_adaptor.h>
#include <devtools/ymake/add_node_context_inline.h>

#include <util/datetime/base.h>
#include <util/string/util.h>
#include <util/string/split.h>

namespace {
    class TVarsEvaluator: public TEvaluatorBase {
    public:
        TVarsEvaluator(const TBuildConfiguration& conf)
            : Vars_(conf.CommandConf)
            , DummyCmd_{conf, nullptr, nullptr}
        {
        }
        ~TVarsEvaluator() override = default;
        TString EvalVarValue(TStringBuf varName) const override {
            return DummyCmd_.SubstVarDeeply(varName, Vars_);
        }
    private:
        const TVars& Vars_;
        mutable TCommandInfo DummyCmd_;
    };

    void ExtractVars(TSet<TStringBuf>& usedVars, TFileContentHolder& incFile) {
        TStringBuf line;
        TStringBuf input = incFile.GetContent();

        while (input.ReadLine(line)) {
            TVector<TStringBuf> words;
            Split(line, " \t", words);
            if (words.size() >= 2 && (words[0] == "#cmakedefine01" || words[0] == "#cmakedefine")) {
                usedVars.insert(words[1]);
            }

            size_t p1, p2 = (size_t)-1;
            while (true) {
                p1 = line.find('@', p2 + 1);
                if (p1 == TStringBuf::npos) {
                    break;
                }

                p2 = line.find('@', p1 + 1);
                if (p2 == TStringBuf::npos) {
                    break;
                }

                TStringBuf found = line.substr(p1 + 1, p2 - p1 - 1);

                static str_spn word("a-zA-Z0-9_-", true);
                size_t wordLen = word.spn(found.data(), found.end());

                if (wordLen == found.size()) {
                    usedVars.insert(found);
                }
            }
        }
    }

    void PrepareFile(TNodeAddCtx& node, TFileContentHolder& incFile) {
        TSet<TStringBuf> usedVars;
        ExtractVars(usedVars, incFile);

        TStringStream cfgVars;
        cfgVars << "Cmd.CfgVars=";
        for (TSet<TStringBuf>::const_iterator var = usedVars.begin(); var != usedVars.end(); ++var) {
            cfgVars << (var == usedVars.begin() ? TStringBuf() : TStringBuf(" ")) << *var;
        }

        node.AddDep(EDT_Property, EMNT_Property, cfgVars.Str());

        TPropertyType propType{node.Graph.Names(), EVI_ModuleProps, "Nuke"};
        ui64 propElemId = node.Graph.Names().AddName(EMNT_Property, FormatProperty("Mod.Nuke", ToString(node.YMake.TimeStamps.CurStamp())));
        TVector props{MakeDepsCacheId(EMNT_Property, propElemId)};
        TPropertySourceDebugOnly sourceDebug{EPropertyAdditionType::Created};
        node.GetEntry().Props.AddValues(propType, props, sourceDebug);
        node.GetEntry().Props.UpdateIntentTimestamp(EVI_ModuleProps, node.YMake.TimeStamps.CurStamp());
    }

    template <auto Value, typename... Args>
    constexpr auto ParserConstructor(Args... args) {
        auto constructor = [args...](TParsersCache* cache, const TEvaluatorBase& evaluator, TSymbols& symbols) {
            if constexpr (std::is_invocable_v<decltype(Value), TParsersCache*, const TEvaluatorBase&, TSymbols&, Args...>) {
                return Value(cache, evaluator, symbols, args...);
            } else if constexpr (std::is_invocable_v<decltype(Value), TParsersCache*, TSymbols&, Args...>) {
                return Value(cache, symbols, args...);
            } else {
                static_assert(std::is_invocable_v<decltype(Value), TParsersCache*, Args...>, "Unable to create a Parser with arguments given, check types");
                return Value(cache, args...);
            }
        };
        return std::function<TParserBaseRef(TParsersCache*, const TEvaluatorBase&, TSymbols&)>(constructor);
    }

    const static auto ParsersAndExtensions = {
        std::make_tuple(EIncludesParserType::EmptyParser, "other", TVector<TStringBuf>({"fml", "fml2", "fml3", "pln", "info", "a", "lua", "sh"}), ParserConstructor<MakeEmptyParser>(false)),
        std::make_tuple(EIncludesParserType::EmptyParser, "other", TVector<TStringBuf>({"bin", "py", "pyi", "pysrc"}), ParserConstructor<MakeEmptyParser>(true)),
        std::make_tuple(EIncludesParserType::CppOnlyParser, "c", TVector<TStringBuf>({"cpp", "cc", "cxx", "c", "C", "auxcpp"}), ParserConstructor<MakeCLikeParser>()),
        std::make_tuple(EIncludesParserType::CppOnlyParser, "c", TVector<TStringBuf>({"h", "hh", "hpp", "cuh", "H", "hxx", "xh", "ipp", "ixx"}), ParserConstructor<MakeCHeaderParser>()),
        std::make_tuple(EIncludesParserType::CppOnlyParser, "c", TVector<TStringBuf>({"cu", "S", "s", "sfdl", "m", "mm"}), ParserConstructor<MakeCppParser>()),
        std::make_tuple(EIncludesParserType::AsmParser, "asm", TVector<TStringBuf>({"asm"}), ParserConstructor<MakeAsmParser>()),
        std::make_tuple(EIncludesParserType::ProtoParser, "proto", TVector<TStringBuf>({"gzt", "gztproto"}), ParserConstructor<MakeGztParser>()),
        std::make_tuple(EIncludesParserType::ProtoParser, "proto", TVector<TStringBuf>({"proto", "ev"}), ParserConstructor<MakeProtoParser>()),
        std::make_tuple(EIncludesParserType::LexParser, "c", TVector<TStringBuf>({"l", "lex", "lpp", "y", "ypp", "gperf", "asp"}), ParserConstructor<MakeLexParser>()),
        std::make_tuple(EIncludesParserType::RagelParser, "ragel", TVector<TStringBuf>({"rl", "rh", "rli", "rl6", "rl5"}), ParserConstructor<MakeRagelParser>()),
        std::make_tuple(EIncludesParserType::MapkitIdlParser, "idl", TVector<TStringBuf>({"idl"}), ParserConstructor<MakeMapkitIdlParser>()),
        std::make_tuple(EIncludesParserType::FortranParser, "c", TVector<TStringBuf>({"f"}), ParserConstructor<MakeFortranParser>()),
        std::make_tuple(EIncludesParserType::XsParser, "xs", TVector<TStringBuf>({"xs"}), ParserConstructor<MakeXsParser>()),
        std::make_tuple(EIncludesParserType::XsynParser, "xsyn", TVector<TStringBuf>({"xsyn"}), ParserConstructor<MakeXsynParser>()),
        std::make_tuple(EIncludesParserType::SwigParser, "swig", TVector<TStringBuf>({"swg"}), ParserConstructor<MakeSwigParser>()),
        std::make_tuple(EIncludesParserType::CythonParser, "cython", TVector<TStringBuf>({"pyx", "pxd", "pxi"}), ParserConstructor<MakeCythonParser>()),
        std::make_tuple(EIncludesParserType::FlatcParser, "flatc", TVector<TStringBuf>({"fbs"}), ParserConstructor<MakeFlatcParser>()),
        std::make_tuple(EIncludesParserType::FlatcParser, "flatc", TVector<TStringBuf>({"fbs64"}), ParserConstructor<MakeFlatcParser64>()),
        std::make_tuple(EIncludesParserType::GoParser, "c", TVector<TStringBuf>({"go"}), ParserConstructor<MakeGoParser>()),
        std::make_tuple(EIncludesParserType::ScParser, "sc", TVector<TStringBuf>({"sc"}), ParserConstructor<MakeScParser>()),
        std::make_tuple(EIncludesParserType::YDLParser, "ydl", TVector<TStringBuf>({"ydl"}), ParserConstructor<MakeYDLParser>()),
        std::make_tuple(EIncludesParserType::NlgParser, "nlg", TVector<TStringBuf>({"nlg"}), ParserConstructor<MakeNlgParser>()),
        std::make_tuple(EIncludesParserType::CfgprotoParser, "proto", TVector<TStringBuf>({"cfgproto"}), ParserConstructor<MakeCfgprotoParser>()),
        std::make_tuple(EIncludesParserType::TsParser, "other", TVector<TStringBuf>({"ts", "js", "tsx", "jsx"}), ParserConstructor<MakeTsParser>()),
        std::make_tuple(EIncludesParserType::RosParser, "ros", TVector<TStringBuf>({"msg", "srv"}), ParserConstructor<MakeRosParser>()),
        std::make_tuple(EIncludesParserType::RosTopicParser, "ros", TVector<TStringBuf>({"rostopic"}), ParserConstructor<MakeRosTopicParser>()),
    };

    const auto& GetLanguagesWithNonPathAddincls() {
        // List of languagues whose ADDINCLs are non-paths
        // Can be used when it's necessary to propagate additional data from peers
        static TSet<TString> languages = {
            "ros",
        };

        return languages;
    }

    struct TLanguagesManager {
        THashMap<EIncludesParserType, TStringBuf> LanguageNameByParserType;
        THashMap<TStringBuf, TLangId> LanguageIdByName;
        THashMap<TStringBuf, TLangId> LanguageIdByExt;
        TVector<TStringBuf> LanguageNameById;
        TVector<TString> LanguageIncludeNameById;
        TSet<TLangId> LanguagesWithNonPathAddincls;

    public:
        inline TLanguagesManager() {
            LanguageNameById.push_back("c");
            LanguageIncludeNameById.push_back("_C__INCLUDE");
            LanguageIdByName.insert(std::make_pair("c", static_cast<TLangId>(0)));
            for (const auto& [parserType, languageName, exts, _] : ParsersAndExtensions) {
                LanguageNameByParserType[parserType] = languageName;
                const auto [it, fresh] = LanguageIdByName.insert(std::make_pair(languageName, static_cast<TLangId>(LanguageNameById.size())));
                if (fresh) {
                    LanguageNameById.push_back(languageName);
                    auto&& incName = TString::Join(TModuleIncDirs::VAR_PREFIX, to_upper(TString(languageName)), TModuleIncDirs::VAR_SUFFIX);
                    LanguageIncludeNameById.push_back(incName);
                }
                for (const auto& ext : exts) {
                    LanguageIdByExt[ext] = it->second;
                }
            }

            for (const auto& name : GetLanguagesWithNonPathAddincls()) {
                const auto it = LanguageIdByName.find(name);
                if (it == LanguageIdByName.end()) {
                    continue;
                }

                LanguagesWithNonPathAddincls.insert(it->second);
            }
        }
    };

    const static TLanguagesManager LanguagesManager;

} // end of anonymous namespace

namespace NLanguages {
    TLangId GetLanguageId(const TStringBuf& name) {
        const auto& languageIdByName = LanguagesManager.LanguageIdByName;
        const auto it = languageIdByName.find(name);
        return it == languageIdByName.end() ? BAD_LANGUAGE : it->second;
    }

    TLangId GetLanguageIdByExt(const TStringBuf& ext) {
        const auto& languageIdByExt = LanguagesManager.LanguageIdByExt;
        const auto it = languageIdByExt.find(ext);
        return it == languageIdByExt.end() ? BAD_LANGUAGE : it->second;
    }

    TLangId GetLanguageIdByParserType(EIncludesParserType parserType) {
        const auto& languageByParserType = LanguagesManager.LanguageNameByParserType;
        if (!languageByParserType.contains(parserType)) {
            return BAD_LANGUAGE;
        }
        return GetLanguageId(languageByParserType.at(parserType));
    }

    TStringBuf GetLanguageName(TLangId languageId) {
        const auto& languageNameById = LanguagesManager.LanguageNameById;
        return languageNameById.at(static_cast<size_t>(languageId));
    }

    const TString& GetLanguageIncludeName(TLangId languageId) {
        const auto& languageIncludeNameById = LanguagesManager.LanguageIncludeNameById;
        return languageIncludeNameById.at(static_cast<size_t>(languageId));
    }

    bool GetLanguageAddinclsAreNonPaths(TLangId languageId) {
        const auto& languagesWithNonPathAddincls = LanguagesManager.LanguagesWithNonPathAddincls;
        return languagesWithNonPathAddincls.contains(languageId);
    }

    TString DumpLanguagesList() {
        TMap<TStringBuf, TVector<TStringBuf>> ExtensionsByLanguage;
        for (const auto& [parserType, languageName, exts, _] : ParsersAndExtensions) {
            auto& data = ExtensionsByLanguage[languageName];
            data.insert(data.end(), exts.begin(), exts.end());
        }
        TStringBuilder str;
        bool first = true;
        for (const auto& [name, exts] : ExtensionsByLanguage) {
            if (!first) {
                str << ", ";
            }
            first = false;
            str << name << " (for";
            for (const auto& ext : exts) {
                str << " ." << ext;
            }
            str << ")";
        }
        return str;
    }

    size_t LanguagesCount() {
        const auto& languageNameById = LanguagesManager.LanguageNameById;
        return languageNameById.size();
    }
}

TIncParserManager::TIncParserManager(const TBuildConfiguration& conf, TSymbols& names)
    : Conf(conf)
    , Names(names)
{
}

TStringBuf TIncParserManager::ExtPreprocess(TStringBuf ext,
                                            const TSymbols& names,
                                            const TAddIterStack& stack) const {
    if (ext.empty() || !Ext2Parser.contains(ext)) {
        Y_ASSERT(stack.size());
        bool found = false;
        for (auto stackItem = stack.rbegin() + 1; stackItem != stack.rend(); stackItem++) {
            if (stackItem->Node.NodeType != EMNT_File && stackItem->Node.NodeType != EMNT_NonParsedFile || stackItem->Dep.DepType != EDT_Include) {
                break;
            }
            Y_ASSERT(UseFileId(stackItem->Node.NodeType));
            TFileView pName = names.FileNameById(stackItem->Node.ElemId);
            TStringBuf newExt = pName.Extension();
            if (Ext2Parser.contains(newExt)) {
                ext = newExt;
                found = true;
                break;
            }
        }
        if (!found) {
            ext = ExtForDefaultParser;
        }
    }

    return ext;
}

void TIncParserManager::ProcessFileWithSubst(TFileContentHolder& incFile, TFileProcessContext context) const {
    if (typeid(context.Node) == typeid(TNodeAddCtx)) {
        TNodeAddCtx& node = static_cast<TNodeAddCtx&>(context.Node);
        PrepareFile(node, incFile);
        Stats.Inc(NStats::EIncParserManagerStats::InFilesCount);
        Stats.Inc(NStats::EIncParserManagerStats::InFilesSize, incFile.Size());
    }
}

void TIncParserManager::ProcessFile(TFileContentHolder& incFile, TFileProcessContext context) const {
    auto fileContext = incFile.GetProcessingContext();
    if (fileContext == ELinkType::ELT_Text) {
        return;
    }

    TStringBuf ext = incFile.GetName().Extension();
    TModuleWrapper wrapper(context.Module, context.Conf, context.ModuleResolveContext);

    if (ext == "in") {
        ProcessFileWithSubst(incFile, context);
        ext = NPath::Extension(incFile.GetName().NoExtension()); // e.g. x.cpp.in -> x.cpp, ext=cpp
    }
    ext = ExtPreprocess(ext, context.ModuleResolveContext.Graph.Names(), context.Stack);

    TParserBase* parser = ParserByExt(ext);
    if (parser) {
        const TIndDepsRule* rule = parser->DepsTransferRules();
        if (rule != nullptr) {
            auto& nodeData = context.ModuleResolveContext.Graph.GetFileNodeData(incFile.GetTargetId());
            nodeData.PassInducedIncludesThroughFiles = rule->PassInducedIncludesThroughFiles;
            nodeData.PassNoInducedDeps = rule->PassNoInducedDeps;
        }
        const auto start = Now();
        auto wasParsed = parser->ParseIncludes(context.Node, wrapper, incFile);
        if (wasParsed) {
            Stats.Inc(NStats::EIncParserManagerStats::ParseTime, (Now() - start).MicroSeconds());
            Stats.Inc(NStats::EIncParserManagerStats::ParsedFilesCount);
            Stats.Inc(NStats::EIncParserManagerStats::ParsedFilesSize, incFile.Size());
        } else {
            Stats.Inc(NStats::EIncParserManagerStats::ParsedFilesRecovered);
        }
    } else {
        YDIAG(VV) << "unknown type of " << incFile.GetName() << " !!!" << Endl;
    }
}

bool TIncParserManager::HasIncludeChanges(TFileContentHolder& incFile, const TParserBase* parser) const {
    if (parser) {
        return parser->HasIncludeChanges(incFile);
    }
    return false;
}

bool TIncParserManager::ProcessOutputIncludes(TFileView outputFileName,
                                              const TVector<TString>& includes,
                                              TModuleWrapper& module,
                                              TAddDepAdaptor& node,
                                              const TSymbols& names,
                                              const TAddIterStack& stack) const {
    TStringBuf ext = ExtPreprocess(outputFileName.Extension(), names, stack);
    TParserBase* parser = ParserByExt(ext);
    if (parser) {
        return parser->ProcessOutputIncludes(node, module, outputFileName, includes);
    } else {
        YDIAG(VV) << "unknown type of " << outputFileName << " !!!" << Endl;
        return false;
    }
}

void TIncParserManager::AddParser(TParserBaseRef parser, const TVector<TString>& extensions, EIncludesParserType type) {
    Y_ASSERT(type < EIncludesParserType::PARSERS_COUNT);
    parser->SetLanguageId(NLanguages::GetLanguageIdByParserType(type));
    parser->SetParserType(type);
    ParsersByType[static_cast<ui32>(type)] = parser;
    for (const auto& ext : extensions) {
        Ext2Parser[ext] = parser;
    }
}

void TIncParserManager::AddParsers(const TParsersList& parsersList) {
    for (const auto& [parser, extensions] : parsersList) {
        parser->RegisterIndDepsRule(Names);
        AddParser(parser, extensions, EIncludesParserType::EmptyParser);
    }
}

bool TIncParserManager::HasParserFor(TStringBuf fileName) const {
    return GetParserFor(fileName) != nullptr;
}

bool TIncParserManager::HasParserFor(TFileView fileName) const {
    return GetParserFor(fileName) != nullptr;
}

TParserBase* TIncParserManager::GetParserFor(TStringBuf fileName) const {
    TStringBuf ext = NPath::Extension(fileName);
    if (ext == "in"sv) {
        ext = NPath::Extension(NPath::NoExtension(fileName));
    }
    return ParserByExt(ext);
}

TParserBase* TIncParserManager::GetParserFor(TFileView fileName) const {
    return GetParserFor(fileName.Basename());
}

void TIncParserManager::SetDefaultParserSameAsFor(TFileView fileName) {
    ExtForDefaultParser = fileName.Extension();
}

void TIncParserManager::InitManager(const TParsersList& parsersList) {
    TVarsEvaluator evaluator(Conf);
    auto* cache = &Cache;

    ParsersByType.resize(static_cast<ui32>(EIncludesParserType::PARSERS_COUNT), nullptr);
    Cache.SetParserTypeToParserIdMapper([this](EIncludesParserType type) {
        if (auto parser = GetParserByType(type)) {
            return parser->GetParserId();
        }
        return TParsersCache::BAD_PARSER_ID;
    });

    for (const auto& [parserType, _, exts, constructor] : ParsersAndExtensions) {
        AddParser(constructor(cache, evaluator, Names), {exts.begin(), exts.end()}, parserType);
    }

    AddParsers(parsersList);
}

TParserBase* TIncParserManager::ParserByExt(const TStringBuf& ext) const {
    if (const TParserBaseRef* p = Ext2Parser.FindPtr(ext)) {
        return p->Get();
    }
    return nullptr;
}

const TIndDepsRule* TIncParserManager::IndDepsRuleByExt(const TStringBuf& ext) const {
    if (TParserBase* pb = ParserByExt(ext)) {
        return pb->DepsTransferRules();
    }
    return nullptr;
}

const TIndDepsRule* TIncParserManager::IndDepsRuleByPath(const TStringBuf& path) const {
    return IndDepsRuleByExt(NPath::Extension(path));
}

const TIndDepsRule* TIncParserManager::IndDepsRuleByPath(TFileView path) const {
    return IndDepsRuleByExt(path.Extension());
}
