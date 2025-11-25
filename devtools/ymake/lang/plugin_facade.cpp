#include "plugin_facade.h"
#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_builder.h>
#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/add_node_context_inline.h>

#include <devtools/ymake/conf.h>
#include <devtools/ymake/parser_manager.h>

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <util/string/cast.h>
#include <library/cpp/string_utils/base64/base64.h>
#include <util/string/subst.h>
#include <util/generic/singleton.h>
#include <util/generic/yexception.h>

void TMacroFacade::InvokeMacro(TPluginUnit& unit, const TStringBuf& name, const TVector<TStringBuf>& params) const {
    THashMap<TString, TSimpleSharedPtr<TMacroImpl>>::const_iterator it = Name2Macro_.find(name);
    if (it == Name2Macro_.end()) {
        ythrow yexception() << "undefined macro with name: " << name;
    }
    it->second->Execute(unit, params);
}

bool TMacroFacade::ContainsMacro(const TStringBuf& name) const {
    return Name2Macro_.find(name) != Name2Macro_.end();
}

TMacroImpl::~TMacroImpl() {
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

class TParserAdapter: public TParserBase {
private:
    TSimpleSharedPtr<TParser> Parser_;
    TIndDepsRule Rule;

public:
    TParserAdapter(TSimpleSharedPtr<TParser> parser)
        : Parser_(parser)
    {
    }

    void RegisterIndDepsRule(TSymbols& symbols) override {
        for (auto ci: Parser_->GetIndDepsRule()) {
            TIndDepsRule::EAction action;

            if (ci.second == "use") {
                action = TIndDepsRule::EAction::Use;

            } else if (ci.second == "pass") {
                action = TIndDepsRule::EAction::Pass;

            } else {
                ythrow yexception() << "Expected (use|pass) action, got " << ci.second;
            }

            Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, ci.first}, action));
        }
        Rule.PassInducedIncludesThroughFiles = Parser_->GetPassInducedIncludes();
    }

    ~TParserAdapter() override {
    }

    bool ProcessOutputIncludes(TAddDepAdaptor&,
                               TModuleWrapper&,
                               TFileView incFileName,
                               const TVector<TString>&) const override {
        //TODO(DEVTOOLS-5291): add proper support of OUTPUT_INCLUDES here
        YConfErr(MacroUse) << "OUTPUT_INCLUDES are not supported for this type of file: "
                           << incFileName << Endl;
        return false;
    }

    bool ParseIncludes(TAddDepAdaptor& node, TModuleWrapper& module, TFileContentHolder& incFile) override {
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

    bool HasIncludeChanges(TFileContentHolder&) const override {
        return true;
    }

    const TIndDepsRule* DepsTransferRules() const override {
        return &Rule;
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
