#include "parser_manager.h"

#include "ymake.h"
#include "makefile_loader.h"
#include "add_node_context_inline.h"
#include "general_parser.h"
#include "module_builder.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/add_dep_adaptor.h>

#include <util/datetime/base.h>
#include <util/string/util.h>
#include <util/string/split.h>

namespace {
    class TVarsEvaluator: public TEvaluatorBase {
    public:
        TVarsEvaluator(const TVars& vars)
            : Vars(vars)
            , DummyCmd{nullptr, nullptr, nullptr}
        {
        }
        ~TVarsEvaluator() override = default;
        TString EvalVarValue(TStringBuf varName) const override {
            return DummyCmd.SubstVarDeeply(varName, Vars);
        }
    private:
        const TVars& Vars;
        mutable TCommandInfo DummyCmd;
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

    const auto& GetLanguagesAndExtensions() {
        // ATTN! Do not change order of elements in this collection and add new elements only to the end.
        static auto languages_and_extensions = {
                std::make_tuple("other", TVector<TStringBuf>({"fml", "fml2", "fml3", "pln", "info", "a", "lua", "sh"}), ParserConstructor<MakeEmptyParser>(false)),
                std::make_tuple("other", TVector<TStringBuf>({"bin", "py", "pysrc"}), ParserConstructor<MakeEmptyParser>(true)),
                std::make_tuple("c", TVector<TStringBuf>({"cpp", "cc", "cxx", "c", "C", "auxcpp"}), ParserConstructor<MakeCLikeParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"h", "hh", "hpp", "cuh", "H", "hxx", "xh", "ipp", "ixx"}), ParserConstructor<MakeCHeaderParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"cu", "S", "s", "sfdl", "m", "mm"}), ParserConstructor<MakeCppParser>()),
                std::make_tuple("asm", TVector<TStringBuf>({"asm"}), ParserConstructor<MakeAsmParser>()),
                std::make_tuple("proto", TVector<TStringBuf>({"gzt", "gztproto"}), ParserConstructor<MakeGztParser>()),
                std::make_tuple("proto", TVector<TStringBuf>({"proto", "ev"}), ParserConstructor<MakeProtoParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"l", "lex", "lpp"}), ParserConstructor<MakeLexParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"y", "ypp"}), ParserConstructor<MakeLexParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"gperf"}), ParserConstructor<MakeLexParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"asp"}), ParserConstructor<MakeLexParser>()),
                std::make_tuple("ragel", TVector<TStringBuf>({"rl", "rh", "rli", "rl6", "rl5"}), ParserConstructor<MakeRagelParser>()),
                std::make_tuple("idl", TVector<TStringBuf>({"idl"}), ParserConstructor<MakeMapkitIdlParser>()),
                std::make_tuple("c", TVector<TStringBuf>({"f"}), ParserConstructor<MakeFortranParser>()),
                std::make_tuple("xs", TVector<TStringBuf>({"xs"}), ParserConstructor<MakeXsParser>()),
                std::make_tuple("xsyn", TVector<TStringBuf>({"xsyn"}), ParserConstructor<MakeXsynParser>()),
                std::make_tuple("swig", TVector<TStringBuf>({"swg"}), ParserConstructor<MakeSwigParser>()),
                std::make_tuple("cython", TVector<TStringBuf>({"pyx", "pxd", "pxi"}), ParserConstructor<MakeCythonParser>()),
                std::make_tuple("flatc", TVector<TStringBuf>({"fbs"}), ParserConstructor<MakeFlatcParser>()),
                std::make_tuple("flatc", TVector<TStringBuf>({"fbs64"}), ParserConstructor<MakeFlatcParser64>()),
                std::make_tuple("c", TVector<TStringBuf>({"go"}), ParserConstructor<MakeGoParser>()),
                std::make_tuple("sc", TVector<TStringBuf>({"sc"}), ParserConstructor<MakeScParser>()),
                std::make_tuple("ydl", TVector<TStringBuf>({"ydl"}), ParserConstructor<MakeYDLParser>()),
                std::make_tuple("nlg", TVector<TStringBuf>({"nlg"}), ParserConstructor<MakeNlgParser>()),
                std::make_tuple("proto", TVector<TStringBuf>({"cfgproto"}), ParserConstructor<MakeCfgprotoParser>()),
                std::make_tuple("other", TVector<TStringBuf>({"ts", "js", "tsx", "jsx"}), ParserConstructor<MakeTsParser>())
        };
        return languages_and_extensions;
    }

    struct TLanguagesManager {
        THashMap<TStringBuf, TLangId> LanguageIdByName;
        THashMap<TStringBuf, TLangId> LanguageIdByExt;
        TVector<TLangId> LanguageIdByParserId;
        TVector<TStringBuf> LanguageNameById;
        TVector<TString> LanguageIncludeNameById;

    public:
        inline TLanguagesManager() {
            LanguageNameById.push_back("c");
            LanguageIncludeNameById.push_back("_C__INCLUDE");
            LanguageIdByName.insert(std::make_pair("c", static_cast<TLangId>(0)));
            LanguageIdByParserId.push_back(static_cast<TLangId>(0));
            for (const auto& language : GetLanguagesAndExtensions()) {
                const auto& name = std::get<0>(language);
                const auto& exts = std::get<1>(language);
                const auto [it, fresh] = LanguageIdByName.insert(std::make_pair(name, static_cast<TLangId>(LanguageNameById.size())));
                if (fresh) {
                    LanguageNameById.push_back(name);
                    auto&& incName = TString::Join(TModuleIncDirs::VAR_PREFIX, to_upper(TString(name)), TModuleIncDirs::VAR_SUFFIX);
                    LanguageIncludeNameById.push_back(incName);
                }
                for (const auto& ext : exts) {
                    LanguageIdByExt[ext] = it->second;
                }
                LanguageIdByParserId.push_back(it->second);
            }
        }

        static inline const TLanguagesManager& Instance() {
            return *Singleton<TLanguagesManager>();
        }
    };

} // end of anonymous namespace

namespace NLanguages {
    TLangId GetLanguageId(const TStringBuf& name) {
        const auto& languageIdByName = TLanguagesManager::Instance().LanguageIdByName;
        const auto it = languageIdByName.find(name);
        return it == languageIdByName.end() ? BAD_LANGUAGE : it->second;
    }

    TLangId GetLanguageIdByExt(const TStringBuf& ext) {
        const auto& languageIdByExt = TLanguagesManager::Instance().LanguageIdByExt;
        const auto it = languageIdByExt.find(ext);
        return it == languageIdByExt.end() ? BAD_LANGUAGE : it->second;
    }

    TLangId GetLanguageIdByParserId(ui32 parserId) {
        const auto& languageByParserId = TLanguagesManager::Instance().LanguageIdByParserId;
        return languageByParserId.at(TParserId(parserId).GetCode());
    }

    TStringBuf GetLanguageName(TLangId languageId) {
        const auto& languageNameById = TLanguagesManager::Instance().LanguageNameById;
        return languageNameById.at(static_cast<size_t>(languageId));
    }

    const TString& GetLanguageIncludeName(TLangId languageId) {
        const auto& languageIncludeNameById = TLanguagesManager::Instance().LanguageIncludeNameById;
        return languageIncludeNameById.at(static_cast<size_t>(languageId));
    }

    TString DumpLanguagesList() {
        TMap<TStringBuf, TVector<TStringBuf>> ExtensionsByLanguage;
        for (const auto& language : GetLanguagesAndExtensions()) {
            const auto& name = std::get<0>(language);
            const auto& exts = std::get<1>(language);
            auto& data = ExtensionsByLanguage[name];
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

    size_t ParsersCount() {
        return GetLanguagesAndExtensions().size();
    }

    size_t LanguagesCount() {
        const auto& languageNameById = TLanguagesManager::Instance().LanguageNameById;
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

void TIncParserManager::AddParser(TParserBaseRef parser, const TVector<TString>& extensions, TLangId languageId) {
    ParsersCount++;
    parser->SetLanguageId(ParsersCount, languageId);
    for (const auto& ext : extensions) {
        Ext2Parser[ext] = parser;
    }
}

void TIncParserManager::AddParsers(const TParsersList& parsersList) {
    for (const auto& [parser, extensions] : parsersList) {
        parser->RegisterIndDepsRule(Names);
        AddParser(parser, extensions, NLanguages::BAD_LANGUAGE);
    }
}

bool TIncParserManager::HasParserFor(TStringBuf fileName) const {
    TStringBuf ext = NPath::Extension(fileName);
    if (ext == "in") {
        ext = NPath::Extension(NPath::NoExtension(fileName));
    }
    return Ext2Parser.contains(ext);
}

bool TIncParserManager::HasParserFor(TFileView file) const {
    return HasParserFor(file.Basename());
}

void TIncParserManager::SetDefaultParserSameAsFor(TFileView fileName) {
    ExtForDefaultParser = fileName.Extension();
}

void TIncParserManager::InitManager(const TParsersList& parsersList) {
    TVarsEvaluator evaluator(Conf.CommandConf);
    auto* cache = &Cache;

    for (const auto& language : GetLanguagesAndExtensions()) {
        const auto& name = std::get<0>(language);
        const auto& exts = std::get<1>(language);
        const auto& constructor = std::get<2>(language);
        AddParser(constructor(cache, evaluator, Names), {exts.begin(), exts.end()}, NLanguages::GetLanguageId(name));
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
