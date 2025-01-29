#pragma once

#include <devtools/ymake/yndex/yndex.h>

#include <devtools/ymake/lang/config_conditions.h>

#include <devtools/ymake/compact_graph/dep_types.h>

#include <devtools/ymake/common/md5sig.h>

#include <devtools/ymake/config/transition.h>

// TODO: fix back includes
#include <devtools/ymake/cmd_properties.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/set.h>
#include <util/generic/string.h>
#include <util/ysaveload.h>

class TModule;

struct TToolOptions {
    TString AddIncl;
    TString AddPeers;

    bool SetProperty(TStringBuf name, TStringBuf value);
    bool SetMultiValueProperty(TStringBuf name, TStringBuf value);

    Y_SAVELOAD_DEFINE(
        AddIncl,
        AddPeers
    );
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
    using TValueType = Type;

    explicit TDefaultValue(const Type& value = Type())
        : Value_(value)
        , IsDefault_(true)
    {
    }

    operator const Type& () const {
        return Value_;
    }

    bool IsDefaultValue() const {
        return IsDefault_;
    }

    void Set(const Type& value) {
        IsDefault_ = false;
        Value_ = value;
    }

    Y_SAVELOAD_DEFINE(
        Value_,
        IsDefault_
    );

private:
    Type Value_;
    bool IsDefault_;
};

struct TModuleConf {
    using TTaggedModules = TMap<TString, TModuleConf*>;
    using TOrderedModules = TVector<std::pair<TString, TModuleConf*>>;
    using TParseModuleArgsFunction = void (*)(TModule* mod, TArrayRef<const TStringBuf> args);
    using TModuleNameFunction = void (*)(TModule* mod);

    inline static const TStringBuf SEM_IGNORED = "IGNORED"sv;

    TString Cmd;
    TString GlobalCmd;
    TString Name;
    TString Tag;
    TString Epilogue;
    THashSet<TString> InputExts;
    THashSet<TString> GlobalInputExts;
    bool AllExtsAreInputs = false;
    bool AllGlobalExtsAreInputs = false;
    TDefaultValue<bool> UseInjectedData = TDefaultValue<bool>(false);
    TDefaultValue<bool> UsePeersLateOuts = TDefaultValue<bool>(false);
    bool IsPackageBundle = false;
    bool IncludeTag = true;
    TDefaultValue<bool> FinalTarget = TDefaultValue<bool>(false);
    TDefaultValue<bool> ProxyLibrary = TDefaultValue<bool>(false);
    TDefaultValue<bool> DepManagementVersionProxy = TDefaultValue<bool>(false);
    EMakeNodeType NodeType;
    ESymlinkType SymlinkType;
    EPeerdirType PeerdirType;
    bool HasSemantics = false;
    bool HasSemanticsForGlobals = false;
    bool StructCmd = false; // Marker requiring to use structured command representation DEVTOOLS-8280
    bool StructCmdSet = false;
    ETransition Transition{ETransition::None};

    THashSet<TString> Restricted;
    THashSet<TString> Ignored;
    THashSet<TString> Allowed;
    TSet<TString> Globals;
    THashMap<TString, TString> MacroAliases;
    TVector<TString>  SpecServiceVars;
    TTaggedModules    SubModules;
    TOrderedModules   OrderedSubModules;
    TVector<TString>  SelfPeers;

    TParseModuleArgsFunction ParseModuleArgs;
    TModuleNameFunction SetModuleBasename;

    TModuleConf()
        : NodeType(EMNT_Deleted)
        , SymlinkType(EST_Unset)
        , PeerdirType(EPT_Unset)
        , ParseModuleArgs(nullptr)
        , SetModuleBasename(nullptr)
    {
    }

    TModuleConf(const TModuleConf&) = default;

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

    bool SetProperty(TStringBuf key, TStringBuf name, TStringBuf val, TVars& vars, bool renderSemantics);

    static bool IsProperty(const TStringBuf name);

    void Inherit(const TModuleConf& parent, bool renderSemantics);

    void ApplyOwnerConf(const TModuleConf& owner);

    void UniteRestrictions();

    void Load(IInputStream* input);

    void Save(IOutputStream* output) const;
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
    TSimpleSharedPtr<TToolOptions> ToolOptions;
    TSimpleSharedPtr<TCmdProperty> CmdProps; // additional properties: keywords, spec conditions(?)
    TSimpleSharedPtr<TModuleConf> ModuleConf;
    TSimpleSharedPtr<TSectionPatterns> SectionPatterns; // <file ext> -> <var name> or <spec param> -> <var name> (for generic macro)

    void Inherit(const TString& name, const TBlockData& parent, bool renderSemantics);

    bool SetOption(TStringBuf blockName, TStringBuf name, TStringBuf value, TVars& commandConf, bool renderSemantics) {
        YDIAG(DG) << "SetOption: " << blockName << "->" << name << "=" << value << Endl;
        if (!GetOrInit(ToolOptions).SetProperty(name, value)) {
            if (!commandConf[blockName].SetOption(name)) {
                if (TModuleConf::IsProperty(name)) {
                    return GetOrInit(ModuleConf).SetProperty(blockName, name, value, commandConf, renderSemantics);
                } else {
                    return false;
                }
            }
        }
        return true;
    }

    void ApplyOwner(const TString& name, TBlockData& owner);

    void Load(IInputStream* input) {
        ::Load(input, ParentName);
        ::Load(input, OwnerName);
        ::Load(input, Completed);
        ::Load(input, IsUserMacro);
        ::Load(input, IsGenericMacro);
        ::Load(input, IsMultiModule);
        ::Load(input, HasPeerdirSelf);
        ::Load(input, HasSemantics);
        ::Load(input, StructCmd);
        ::Load(input, IsFileGroupMacro);

        auto loadPointer = [](IInputStream* input, auto& item) {
            bool flag;
            ::Load(input, flag);
            if (flag) {
                using PointerType = std::remove_reference_t<decltype(std::declval<decltype(*item)>())>;
                PointerType temp;
                ::Load(input, temp);
                item = MakeHolder<PointerType>(temp);
            } else {
                item = nullptr;
            }
        };
        loadPointer(input, ToolOptions);
        loadPointer(input, CmdProps);
        loadPointer(input, ModuleConf);
        loadPointer(input, SectionPatterns);
    }

    void Save(IOutputStream* output) const {
        ::Save(output, ParentName);
        ::Save(output, OwnerName);
        ::Save(output, Completed);
        ::Save(output, IsUserMacro);
        ::Save(output, IsGenericMacro);
        ::Save(output, IsMultiModule);
        ::Save(output, HasPeerdirSelf);
        ::Save(output, HasSemantics);
        ::Save(output, StructCmd);
        ::Save(output, IsFileGroupMacro);

        auto savePointer = [](IOutputStream* output, auto& item) {
            if (item) {
                ::Save(output, true);
                ::Save(output, *item);
            } else {
                ::Save(output, false);
            }
        };
        savePointer(output, ToolOptions);
        savePointer(output, CmdProps);
        savePointer(output, ModuleConf);
        savePointer(output, SectionPatterns);
    }
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

struct TImportedFileDescription {
    TString FileName;
    TString FileHash;
    time_t ModificationTime;

    Y_SAVELOAD_DEFINE(
        FileName,
        FileHash,
        ModificationTime
    );
};

struct TYmakeConfig {
    TVars CommandConf;     //command patterns, compilers, flags from ymake.conf
    TCondition Conditions; //THashMap: mapping from Var-name to (condition numbers+actions) that have to be recalculated
    THashMap<TString, TBlockData> BlockData;

    TMd5Sig YmakeConfMD5;
    TMd5Sig YmakeConfWoRulesMD5;
    TMd5Sig YmakeExtraConfMD5;

    TMd5Sig YmakeBlacklistHash;
    TMd5Sig YmakeIsolatedProjectsHash;

    THashSet<TString> ReservedNames;
    THashSet<TString> ResourceNames; // Strictly speaking these are not part of Ymake config hence arriving late

    NYndex::TDefinitions CommandDefinitions;

    TVector<TImportedFileDescription> ImportedFiles;

    bool RenderSemantics = false; // Enable render semantics instead commands
    bool ForeignOnNoSem = false; // On NoSem error make TForeignPlatformTarget event instead TConfigureError

    void ClearYmakeConfig();

    void CompleteModules();
    void FillInheritedData(TBlockData& data, const TString& name);
    void VerifyModuleConfs();

    TStringBuf GetMacroByExt(const TStringBuf& sectionName, const TStringBuf& fname) const;
    TStringBuf GetSpecMacroName(const TStringBuf& macroName, const TVector<TStringBuf>& args) const;

    void LoadConfig(TStringBuf path, TStringBuf sourceRoot, TStringBuf buildRoot, MD5& confData);
    void LoadConfigFromContext(TStringBuf context);

    // FIXME DEPRECATED: Temporary hacky workaround for external resources collection into globals
    void RegisterResourceLate(const TString& variableName, const TString& value) const;

    Y_SAVELOAD_DEFINE(
        CommandConf,
        Conditions,
        BlockData,
        CommandDefinitions
    );

    bool GetFromCache() const {
        return FromCache_;
    }

    void SetFromCache(bool fromCache) {
        FromCache_ = fromCache;
    }
private:
    bool FromCache_ = false;
};
