#include "plugin_facade.h"
#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_builder.h>
#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/add_node_context_inline.h>

#include <devtools/ymake/conf.h>
#include <devtools/ymake/parser_manager.h>

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <library/cpp/string_utils/base64/base64.h>

#include <util/string/cast.h>
#include <util/string/subst.h>
#include <util/generic/singleton.h>
#include <util/generic/serialized_enum.h>
#include <util/generic/yexception.h>

#include <fmt/format.h>

#include <algorithm>

namespace {

TString AllActions() {
    TStringStream out;
    bool first = true;
    for (const auto& [_, name]: GetEnumNames<TIndDepsRule::EAction>()) {
        if (!std::exchange(first, false))
            out << '|';
        out << name;
    }
    return out.Str();
}

}

TMacroImpl* TMacroFacade::FindMacro(TStringBuf name) const {
    auto it = Name2Macro_.find(name);
    return it != Name2Macro_.end() ? it->second.get() : nullptr;
}

void TMacroFacade::RegisterMacro(TBuildConfiguration& conf, const TString& name, TSimpleSharedPtr<TMacroImpl> action) {
    Name2Macro_.try_emplace(name, action);
    const auto& def = action->Definition;
    conf.CommandDefinitions.AddDefinition(
        name,
        def.FilePath,
        {def.LineBegin, def.ColumnBegin, def.LineEnd, def.ColumnEnd},
        def.DocText,
        NYndex::EDefinitionType::Macro);
}

void TMacroFacade::Clear() {
    Name2Macro_.clear();
}

class TParserAdapter: public TUserParserBase {
private:
    TSimpleSharedPtr<TParser> Parser_;
    TIndDepsRule Rule_;

public:
    TParserAdapter(TSimpleSharedPtr<TParser> parser)
        : Parser_(parser)
    {
    }

    void RegisterIndDepsRule(TSymbols& symbols) override {
        for (const auto& [type, actionStr]: Parser_->GetIndDepsRule()) {
            TIndDepsRule::EAction action;
            if (!TryFromString(actionStr, action))
                ythrow yexception() << "Expected (" << AllActions() << ") action, got " << actionStr;

            Rule_.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, type}, action));
        }
        Rule_.PassInducedIncludesThroughFiles = Parser_->GetPassInducedIncludes();

        if (auto* base = BaseParser()) {
            const auto baseRules = base->DepsTransferRules();
            if (Rule_.PassInducedIncludesThroughFiles != baseRules.PassInducedIncludesThroughFiles)
                YConfErr(Conf) << "Attempt to register user parser specialization conflicting with base parser rule of propagation through include dependencies!" << Endl;
            Rule_.PassNoInducedDeps = baseRules.PassNoInducedDeps;
            std::ranges::copy(baseRules.Actions, std::back_inserter(Rule_.Actions));
        }
    }

    ~TParserAdapter() noexcept override = default;

    TVector<TString> MapProps(TSymbols& symbols, TPropertyType type, const TVector<TStringBuf>& props) const override {
        return Parser_->MapProps(type.GetName(symbols), props);
    }

    bool DoParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) override {
        TVector<TString> includes;
        TPyDictReflection inducedDeps;
        Parser_->Execute(TString(incFile.GetAbsoluteName()), module, includes, inducedDeps);
        for (const auto& include : includes) {
            node.AddUniqueDep(EDT_Include, FileTypeByRoot(include), include);
        }

        if (!inducedDeps.empty()) {
            for (auto ind : inducedDeps) {
                auto& includes = ind.second;
                if (!includes.empty()) {
                    TVector<TResolveFile> includeViews;
                    includeViews.reserve(ind.second.size());
                    for (auto& include: includes) {
                        includeViews.emplace_back(module.AssumeResolved(include));
                    }
                    node.AddParsedIncls(ind.first, includeViews);
                }
            }
        }

        return true;
    }

    bool DoHasIncludeChanges(TFileContentHolder&) const override {
        return true;
    }

    const TIndDepsRule& DepsTransferRules() const override {
        return Rule_;
    }
};

void TMacroFacade::RegisterParser(TBuildConfiguration& conf, const TString& ext, TSimpleSharedPtr<TParser> parser) {
    TVector<TString> exts;
    exts.push_back(ext);
    conf.ParserPlugins.push_back(std::make_pair(new TParserAdapter(parser), exts));
}

void RegisterPluginFilename(TBuildConfiguration& conf, const char* fileName) {
    conf.Plugins.push_back(fileName);
}

void OnPluginLoadFail(const char* fileName, const char* msg) {
    YErr() << "Failed to load ymake plugin " << fileName << " with message: \"" << msg << "\"\n";
}

void OnConfigureError(const char* msg) {
    YConfErr(Misconfiguration) << msg << Endl;
}

void OnBadDirError(const char* msg, const char* dir) {
    TRACE(P, NEvent::TInvalidSrcDir(TString{dir}));
    YConfErr(BadDir) << msg << Endl;
}
