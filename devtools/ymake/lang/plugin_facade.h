#pragma once

#include <devtools/ymake/macro_vars.h>
#include <devtools/ymake/symbols/name_store.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>
#include <util/generic/map.h>

class TBuildConfiguration;

class TYMake;

class TResolveFile;

struct TCmdDescription {
    TString Prefix;
    TString PrefixColor;
    TVector<TString> Paths;
};

typedef TMap<TString, TVector<TString>> TPyDictReflection;

class TPluginUnit: private TNonCopyable {
public:
    virtual void CallMacro(TStringBuf name, const TVector<TStringBuf>& args) = 0;
    virtual void CallMacro(TStringBuf name, const TVector<TStringBuf>& args, [[maybe_unused]] TVars extraVars) {
        CallMacro(name, args);
    }

    virtual TStringBuf Get(TStringBuf name) const = 0;

    virtual std::variant<TStringBuf, TString> GetSubst(TStringBuf name) const = 0;

    virtual void Set(TStringBuf name, TStringBuf value) = 0;

    virtual TString ResolveToAbsPath(TStringBuf path) = 0;

    virtual TString ResolveToArcPath(TStringBuf path, bool force = false) = 0;

    virtual TString ResolveToBinDirLocalized(TStringBuf path) = 0;

    virtual bool Enabled(TStringBuf path) const = 0;

    virtual TString UnitName() const = 0;

    virtual TString UnitFileName() const = 0;

    virtual TString GetGlobalFileName() const = 0;

    virtual TString UnitPath() const = 0;

    virtual void ResolveInclude(TStringBuf src, const TVector<TStringBuf>& includes, TVector<TString>& result) = 0;

    virtual void SetProperty(TStringBuf propName, TStringBuf value) = 0;

    virtual void AddDart(TStringBuf dartName, TStringBuf dartValue, const TVector<TStringBuf>& vars) = 0;

    virtual ~TPluginUnit() {
    }
};

class TMacroImpl {
public:
    virtual void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params) = 0;

    virtual ~TMacroImpl();

    struct TDefinition {
        TString DocText;
        TString FilePath;
        size_t LineBegin = 0;
        size_t ColumnBegin = 0;
        size_t LineEnd = 0;
        size_t ColumnEnd = 0;
    } Definition;

};

class TParser {
public:
    virtual void Execute(const TString& path, TPluginUnit& unit, TVector<TString>& includes, TPyDictReflection& inducedDeps) = 0;
    virtual const std::map<TString, TString>& GetIndDepsRule() const = 0;
    virtual bool GetPassInducedIncludes() const = 0;

    virtual ~TParser() {
    }
};

class TMacroFacade {
private:
    THashMap<TString, TSimpleSharedPtr<TMacroImpl>> Name2Macro_;

public:
    void InvokeMacro(TPluginUnit& unit, const TStringBuf& name, const TVector<TStringBuf>& params) const;
    bool ContainsMacro(const TStringBuf& name) const;

    void RegisterMacro(TBuildConfiguration& conf, const TString& name, TSimpleSharedPtr<TMacroImpl> action);
    void RegisterParser(TBuildConfiguration& conf, const TString& ext, TSimpleSharedPtr<TParser> parser);

    void Clear();
};

// functions below implemented outside

void LoadPlugins(const TVector<TFsPath> &pluginsRoot, const TFsPath& pycache, TBuildConfiguration *conf);

void RegisterPluginFilename(TBuildConfiguration& conf, const char* fileName);

void OnPluginLoadFail(const char* fileName, const char* msg);

void OnConfigureError(const char* msg);
void OnBadDirError(const char* msg, const char* dir);
