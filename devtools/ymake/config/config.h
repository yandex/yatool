#pragma once

#include <devtools/ymake/yndex/yndex.h>

#include <devtools/ymake/lang/config_conditions.h>

#include <devtools/ymake/compact_graph/dep_types.h>

#include <devtools/ymake/common/md5sig.h>

// TODO: fix back includes
#include <devtools/ymake/cmd_properties.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/hash_set.h>
#include <util/generic/set.h>
#include <util/generic/string.h>

class TModule;

struct TToolOptions {
    TString AddIncl;
    TString AddPeers;

    TToolOptions()
    {
    }

    bool SetOption(TStringBuf name, TStringBuf value);
    bool SetMultiValueOption(TStringBuf name, TStringBuf value);
};

enum ESymlinkType {
    EST_None = 0,
    EST_Exe = 1,
    EST_So = 2,
    EST_Unset = 3,
};

enum EPeerdirType {
    EPT_BuildFrom = 0,
    EPT_Include = 1,
    EPT_Unset = 3,
};

template <typename Type>
class TDefaultValue {
public:
    TDefaultValue(const Type& value = Type())
        : Value(value)
    {
    }

    TDefaultValue& operator=(const TDefaultValue& value) {
        if (this != &value) {
            Value = value.Value;
            IsDefault = false;
        }
        return *this;
    }

    operator const Type& () const {
        return Value;
    }

    bool IsDefaultValue() const {
        return IsDefault;
    }

private:
    Type Value;
    bool IsDefault = true;
};

struct TModuleConf {
    using TTaggedModules = TMap<TString, TModuleConf*>;
    using TOrderedModules = TVector<std::pair<TString, TModuleConf*>>;

    TString Cmd;
    TString CmdIgnore; // TODO this should be part of Cmd metadata
    TString GlobalCmd;
    TString Name;
    TString Tag;
    TString Epilogue;
    THashSet<TString> InputExts;
    THashSet<TString> GlobalInputExts;
    bool AllExtsAreInputs = false;
    bool AllGlobalExtsAreInputs = false;
    TDefaultValue<bool> UseInjectedData = false;
    TDefaultValue<bool> UsePeersLateOuts = false;
    bool IsPackageBundle = false;
    bool IncludeTag = true;
    TDefaultValue<bool> FinalTarget = false;
    TDefaultValue<bool> ProxyLibrary = false;
    EMakeNodeType NodeType;
    ESymlinkType SymlinkType;
    EPeerdirType PeerdirType;
    bool HasSemantics = false;
    bool HasSemanticsForGlobals = false;
    bool StructCmd = false; // Marker requiring to use structured command representation DEVTOOLS-8280

    THashSet<TString> Restricted;
    THashSet<TString> Ignored;
    THashSet<TString> Allowed;
    TSet<TString> Globals;
    THashMap<TString, TString> MacroAliases;
    TVector<TString>  SpecServiceVars;
    TTaggedModules    SubModules;
    TOrderedModules   OrderedSubModules;
    TVector<TString>  SelfPeers;

    TModuleConf()
        : NodeType(EMNT_Deleted)
        , SymlinkType(EST_Unset)
        , PeerdirType(EPT_Unset)
        , ParseModuleArgs(nullptr)
        , SetModuleBasename(nullptr)
    {
    }

    inline bool IsInput(TStringBuf fileExt) const {
        return AllExtsAreInputs || InputExts.contains(fileExt);
    }

    inline bool IsGlobalInput(TStringBuf fileExt) const {
        return AllGlobalExtsAreInputs || GlobalInputExts.contains(fileExt);
    }

    inline void AddExt(TStringBuf fileExt, bool global) {
        bool& allInputs = global ? AllGlobalExtsAreInputs : AllExtsAreInputs;
        auto& exts = global ? GlobalInputExts : InputExts;
        if (fileExt == ".*") {
            allInputs = true;
        } else {
            if (fileExt.at(0) == '.')
                fileExt.Skip(1);
            exts.insert(TString{fileExt});
        }
    }

    bool AddSubmodule(const TString& tag, TModuleConf& sub);

    bool SetOption(TStringBuf key, TStringBuf name, TStringBuf val, TVars& vars, bool renderSemantics);

    void (*ParseModuleArgs)(TModule* mod, TArrayRef<const TStringBuf> args);

    void (*SetModuleBasename)(TModule* mod);

    static bool IsOption(const TStringBuf name);

    void Inherit(const TModuleConf& parent);

    void ApplyOwnerConf(const TModuleConf& owner);

    void UniteRestrictions();
};

using TSectionPatterns = THashMap<TString, TString>;

template <typename SmartPtr, typename T = typename std::remove_pointer_t<decltype(std::declval<SmartPtr>().Get())> >
T& GetOrInit(SmartPtr& ptr) {
    if (!ptr.Get()) {
        ptr.Reset(new T);
    }
    return *ptr.Get();
}

struct TBlockData {
    TString ParentName;
    TString OwnerName;
    bool Completed = false; //all fields are updated and ready
    bool IsUserMacro = false;
    bool IsGenericMacro = false;
    bool IsMultiModule = false;
    bool HasPeerdirSelf = false;
    bool HasSemantics = false;
    bool StructCmd = false; // Marker requiring to use structured command representation DEVTOOLS-8280
    bool IsFileGroupMacro = false;
    THolder<TToolOptions> ToolOptions;
    THolder<TCmdProperty> CmdProps; // additional properties: keywords, spec conditions(?)
    THolder<TModuleConf> ModuleConf;
    THolder<TSectionPatterns> SectionPatterns; // <file ext> -> <var name> or <spec param> -> <var name> (for generic macro)

    void Inherit(const TString& name, const TBlockData& parent);

    bool SetOption(TStringBuf blockName, TStringBuf name, TStringBuf value, TVars& commandConf, bool renderSemantics) {
        YDIAG(DG) << "SetOption: " << blockName << "->" << name << "=" << value << Endl;
        if (!GetOrInit(ToolOptions).SetOption(name, value)) {
            if (!commandConf[blockName].SetOption(name)) {
                if (TModuleConf::IsOption(name)) {
                    return GetOrInit(ModuleConf).SetOption(blockName, name, value, commandConf, renderSemantics);
                } else {
                    return false;
                }
            }
        }
        return true;
    }

    void ApplyOwner(const TString& name, TBlockData& owner);
};

inline bool IsBlockDataModule(const TBlockData* blockData) {
    return blockData && blockData->ModuleConf;
}

inline bool IsBlockDataMultiModule(const TBlockData* blockData) {
    return IsBlockDataModule(blockData) && !blockData->ModuleConf->SubModules.empty();
}

inline bool IsFileExtRule(const TBlockData* blockData) {
    return blockData && blockData->SectionPatterns && !blockData->IsGenericMacro;
}

inline bool HasNamedArgs(const TBlockData* blockData) {
    return blockData && blockData->CmdProps && blockData->CmdProps->IsNonPositional();
}

struct TYmakeConfig {
    TVars CommandConf;     //command patterns, compilers, flags from ymake.conf
    TCondition Conditions; //THashMap: mapping from Var-name to (condition numbers+actions) that have to be recalculated
    THashMap<TString, TBlockData> BlockData;

    TMd5Sig YmakeConfMD5;
    TMd5Sig YmakeConfWoRulesMD5;
    TMd5Sig YmakeExtraConfMD5;

    THashSet<TString> ReservedNames;
    THashSet<TString> ResourceNames; // Strictly speaking these are not part of Ymake config hence arriving late

    NYndex::TDefinitions CommandDefinitions;

    bool RenderSemantics = false;

    void CompleteModules();
    void FillInheritedData(TBlockData& data, const TString& name);
    void VerifyModuleConfs();

    TStringBuf GetMacroByExt(const TStringBuf& sectionName, const TStringBuf& fname) const;
    TStringBuf GetSpecMacroName(const TStringBuf& macroName, const TVector<TStringBuf>& args) const;

    void LoadConfig(TStringBuf path, TStringBuf sourceRoot, TStringBuf buildRoot, MD5& confData);
    void LoadConfigFromContext(TStringBuf context);

    // FIXME DEPRECATED: Temporary hacky workaround for external resources collection into globals
    void RegisterResourceLate(const TString& variableName, const TString& value) const;
};
