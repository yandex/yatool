#pragma once

#include "module_resolver.h"

#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/lang/plugin_facade.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/string/strip.h>

struct TInclude;

/// @brief This class provides access to module state in TModule via TPluginUnit interface.
/// It implements all parts of interface except YMake graph modifications
/// (SetProperty(), CallMacro() and AddDart() methods). The latter three are implemented in TModuleBuilder.
/// Objects of this class are passed to parsers: those are prohibited to modify graph directly.
class TModuleWrapper: public TModuleResolver, public TPluginUnit {
public:
    TModuleWrapper(TModule& module, const TBuildConfiguration& conf, const TModuleResolveContext& ctx)
        : TModuleResolver(module, conf, ctx)
    {
    }

    TStringBuf Get(TStringBuf name) const override {
        return Module.Get(name);
    }
    std::variant<TStringBuf, TString> GetSubst(TStringBuf name) const override {
        const auto value = Module.Get(name);
        if (!value.IsInited() || !Module.Vars.Base || !value.contains('$')) {
            return value; // !IsInited() - return None in Python
        }
        return Strip(TCommandInfo(Conf, nullptr, nullptr).SubstVarDeeply(name, Module.Vars));
    }
    void Set(TStringBuf name, TStringBuf value) override {
        return Module.Set(name, value);
    }
    bool IsTrue(TStringBuf name) const {
        return Module.Vars.IsTrue(name);
    }

    bool Enabled(TStringBuf path) const override {
        return Module.Enabled(path);
    }

    TString UnitName() const override {
        return Module.UnitName();
    }

    TString UnitFileName() const override {
        return Module.UnitFileName();
    }

    TString GetGlobalFileName() const override {
        return TString{Module.GetGlobalFileName().GetTargetStr()};
    }

    TString UnitPath() const override {
        return TString(Module.UnitPath());
    }

    void SetProperty(TStringBuf propName, TStringBuf value) override {
        ythrow TError() << "SetProperty (" << propName << "=" << value << "): Graph modification is no allowed in a simple wrapper";
    }

    void AddDart(TStringBuf dartName, TStringBuf dartValue, const TVector<TStringBuf>&) override {
        ythrow TError() << "AddDart (" << dartName << "=" << dartValue << "): Graph modification is no allowed in a simple wrapper";
    }

    void CallMacro(TStringBuf name, const TVector<TStringBuf>&) override {
        ythrow TError() << "CallMacro (" << name << ") is not allowed from a simple wrapper";
    }

    //
    // This in fact calls ResolveLocalIncludes()
    // TODO: Update plugin interface to reflect semantics
    void ResolveInclude(TStringBuf src, const TVector<TStringBuf>& includes, TVector<TString>& result) override;

    TString ResolveToAbsPath(TStringBuf path) override;
    TString ResolveToArcPath(TStringBuf path, bool force = false) override;

    TString ResolveToBinDirLocalized(TStringBuf path) override {
        return ResolveToModuleBinDirLocalized(path);
    }

};
