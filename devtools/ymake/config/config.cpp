#include "config.h"

#include <devtools/ymake/module_confs.h>
#include <devtools/ymake/lang/confreader_cache.h>

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/lang/properties.h>

#include <library/cpp/iterator/mapped.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/generic/scope.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/cast.h>
#include <util/string/strip.h>

namespace {
    using TTaggedModules = TModuleConf::TTaggedModules;
    using TOrderedModules = TModuleConf::TOrderedModules;

    constexpr TStringBuf INHERITED = ".INHERITED"sv;

    enum class EState {
        NotStarted,
        Started,
        Complete
    };

    struct TInfo {
        THashMap<TStringBuf, EState> States;
        TOrderedModules Ordered;
        TVector<TStringBuf> Stack;
        bool HasSelfPeers = false;
    };

    bool OrderSubModulesRecursively(TStringBuf multiModuleName, TTaggedModules& subModules, const TString& modTag, TInfo* info) {
        Y_ASSERT(subModules.contains(modTag));
        Y_ASSERT(info != nullptr);

        auto& [states, ordered, stack, hasSelfPeers] = *info;

        stack.push_back(modTag);
        Y_DEFER {
            info->Stack.pop_back();
        };

        auto& state = states[modTag];
        if (state == EState::NotStarted) {
            state = EState::Started;
            auto& selfPeers = subModules[modTag]->SelfPeers;
            for (const auto& tag : selfPeers) {
                if (!OrderSubModulesRecursively(multiModuleName, subModules, tag, info)) {
                    return false;
                }
            }
            state = EState::Complete;
            ordered.emplace_back(modTag, subModules[modTag]);
            hasSelfPeers = hasSelfPeers || !selfPeers.empty();
        } else if (state == EState::Started) {
            auto index = FindIndex(stack, modTag);
            Y_ASSERT(index != NPOS);
            auto&& names = MakeMappedRange(stack.begin() + index, stack.end(), [](auto name) {
                return TString::Join(TStringBuf("[[alt1]]"), name, TStringBuf("[[rst]]"));
            });
            YConfErr(BadDep) << "A loop has been detected between sub-modules of multi-module [[alt1]]"
                << multiModuleName << "[[rst]]: "
                << JoinStrings(names.begin(), names.end(), TStringBuf(" -> ")) << Endl;
            return false;
        }

        Y_ASSERT(state == EState::Complete);
        return true;
    }

    bool OrderSubModules(TStringBuf multiModuleName, TTaggedModules& subModules, TOrderedModules* ordered) {
        Y_ASSERT(ordered != nullptr);

        TInfo info;
        bool ok = true;
        for (auto& [tag, moduleConf] : subModules) {
            if (!OrderSubModulesRecursively(multiModuleName, subModules, tag, &info)) {
                ok = false;
                break;
            }
        }

        Y_ASSERT(subModules.size() == info.Ordered.size() || !ok);
        if (ok) {
            ordered->swap(info.Ordered);
        } else {
            ordered->clear();
            for (auto& [tag, modConf] : subModules) {
                ordered->emplace_back(tag, modConf);
                modConf->SelfPeers.clear();
            }
        }

        return ok && info.HasSelfPeers;
    }

    void ReportUnexpectedValueForProperty(const TStringBuf blockName, const TStringBuf propName, const TStringBuf value) {
        YErr() << "Unexpected value [" << value << "] for property ." << propName << " in [" << blockName << "]" << Endl;
    }

    template<typename TDest>
    void ApplyBoolProperty(TDest& dest, const TStringBuf key, const TStringBuf name, const TStringBuf value) {
        if (IsTrue(value)) {
            dest = true;
        } else if (IsFalse(value)) {
            dest = false;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    }

    template<typename Type>
    void ApplyBoolProperty(TDefaultValue<Type>& dest, const TStringBuf key, const TStringBuf name, const TStringBuf value) {
        if (IsTrue(value)) {
            dest.Set(true);
        } else if (IsFalse(value)) {
            dest.Set(false);
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    }

    const TString NullFuncName = "";

    const TString DirNameFuncName = "DirName";
    const TString UseDirFuncName = "UseDirNameOrSetGoPackage";
    const TString TwoDirFuncName = "TwoDirNames";
    const TString ThreeDirFuncName = "ThreeDirNames";
    const TString FullPathFuncName = "FullPath";

    const TString& ModuleNameFunctionToString(TModuleConf::TModuleNameFunction func) {
        if (func == nullptr) {
            return NullFuncName;
        } else if (func == SetDirNameBasename) {
            return DirNameFuncName;
        } else if (func == SetDirNameBasenameOrGoPackage) {
            return UseDirFuncName;
        } else if (func == SetTwoDirNamesBasename) {
            return TwoDirFuncName;
        } else if (func == SetThreeDirNamesBasename) {
            return ThreeDirFuncName;
        } else if (func == SetFullPathBasename) {
            return FullPathFuncName;
        }
        Y_ASSERT(false);
        return NullFuncName;
    }

    TModuleConf::TModuleNameFunction StringToModuleNameFunction(const TString& str) {
        if (str == NullFuncName) {
            return nullptr;
        } else if (str == DirNameFuncName) {
            return SetDirNameBasename;
        } else if (str == UseDirFuncName) {
            return SetDirNameBasenameOrGoPackage;
        } else if (str == TwoDirFuncName) {
            return SetTwoDirNamesBasename;
        } else if (str == ThreeDirFuncName) {
            return SetThreeDirNamesBasename;
        } else if (str == FullPathFuncName) {
            return SetFullPathBasename;
        }
        Y_ASSERT(false);
        return nullptr;
    }

    const TString ParseDllFuncName = "DLL";
    const TString ParseBaseFuncName = "Base";
    const TString ParseRawFuncName = "Raw";

    const TString& ParseModuleArgsFunctionToString(TModuleConf::TParseModuleArgsFunction func) {
        if (func == nullptr) {
            return NullFuncName;
        } else if (func == ParseDllModuleArgs) {
            return ParseDllFuncName;
        } else if (func == ParseBaseModuleArgs) {
            return ParseBaseFuncName;
        } else if (func == ParseRawModuleArgs) {
            return ParseRawFuncName;
        }
        Y_ASSERT(false);
        return NullFuncName;
    }

    TModuleConf::TParseModuleArgsFunction StringToParseModuleArgsFunction(const TString& str) {
        if (str == NullFuncName) {
            return nullptr;
        } else if (str == ParseDllFuncName) {
            return ParseDllModuleArgs;
        } else if (str == ParseBaseFuncName) {
            return ParseBaseModuleArgs;
        } else if (str == ParseRawFuncName) {
            return ParseRawModuleArgs;
        }
        Y_ASSERT(false);
        return nullptr;
    }
}

template<typename TInput, typename TOutput>
inline void CopyParent(const TInput& from, TOutput& to) {
    to.insert(from.begin(), from.end());
}

static TStringBuf GetSectionPattern(const TBlockData& blockData, const TStringBuf& patternName) {
    if (const auto& patterns = blockData.SectionPatterns) {
        auto it = patterns->find(patternName);
        if (it != patterns->end()) {
            return it->second;
        }
        it = patterns->find("*");
        if (it != patterns->end()) {
            return it->second;
        }
    }
    return TStringBuf();
}

static TStringBuf GetSectionPattern(const decltype(TYmakeConfig::BlockData)& blockData, const TStringBuf& sectionName, const TStringBuf& patternName) {
    const auto it = blockData.find(sectionName);
    if (it == blockData.end() || !it->second.SectionPatterns) {
        return sectionName;
    }
    return GetSectionPattern(it->second, patternName);
}

bool TToolOptions::SetMultiValueProperty(TStringBuf name, const TStringBuf value) {
    if (!EqualToOneOf(name, NProperties::ADDINCL, NProperties::PEERDIR)) {
        return false;
    }
    TString& property = name == NProperties::ADDINCL ? AddIncl : AddPeers;
    property = property.empty() ? TString(value) : TString::Join(property, " ", value);
    return true;
}

bool TToolOptions::SetProperty(TStringBuf name, TStringBuf value) {
    return SetMultiValueProperty(name, value);
}

bool TModuleConf::IsProperty(const TStringBuf name) {
    static const THashSet<TStringBuf> properties{
        NProperties::ALIASES,
        NProperties::ALLOWED,
        NProperties::CMD,
        NProperties::STRUCT_CMD,
        NProperties::STRUCT_SEM,
        NProperties::DEFAULT_NAME_GENERATOR,
        NProperties::ARGS_PARSER,
        NProperties::GLOBAL,
        NProperties::GLOBAL_CMD,
        NProperties::GLOBAL_EXTS,
        NProperties::GLOBAL_SEM,
        NProperties::EPILOGUE,
        NProperties::EXTS,
        NProperties::FINAL_TARGET,
        NProperties::IGNORED,
        NProperties::INCLUDE_TAG,
        NProperties::NODE_TYPE,
        NProperties::PEERDIRSELF,
        NProperties::PEERDIR_POLICY,
        NProperties::PROXY,
        NProperties::RESTRICTED,
        NProperties::SEM,
        NProperties::SYMLINK_POLICY,
        NProperties::USE_INJECTED_DATA,
        NProperties::USE_PEERS_LATE_OUTS,
        NProperties::FILE_GROUP,
        NProperties::TRANSITION
    };
    return properties.contains(name);
}

void TModuleConf::Inherit(const TModuleConf& parent, bool renderSemantics) {
    if (!renderSemantics) { // commands
        if (Cmd == INHERITED) {
            YConfWarn(Syntax) << INHERITED << " in commands not supported" << Endl;
        }
        if (Cmd.empty() && !parent.Cmd.empty()) {
            Cmd = parent.Cmd;
        }
        if (GlobalCmd == INHERITED) {
            YConfWarn(Syntax) << INHERITED << " in GLOBAL commands not supported" << Endl;
        }
        if (GlobalCmd.empty() && !parent.GlobalCmd.empty()) {
            GlobalCmd = parent.GlobalCmd;
        }
    } else { // semantics
        bool noSem = false; // for report both NoSem errors before return
        if (Cmd == INHERITED) {
            if (!parent.HasSemantics) {
                YConfErr(NoSem) << "Semantics in the parent module of " << Name << " doesn't exists" << Endl;
                HasSemantics = false;
                noSem = true;
            } else {
                Cmd = parent.Cmd;
            }
        }
        if (GlobalCmd == INHERITED) {
            if (!parent.HasSemanticsForGlobals) {
                YConfErr(NoSem) << "Semantics of GLOBAL SRCS in the parent module of " << Name << " doesn't exists" << Endl;
                HasSemanticsForGlobals = false;
                noSem = true;
            } else {
                Cmd = parent.GlobalCmd;
            }
        }
        if (noSem) {
            return;
        }
    }

    if (!StructCmdSet) {
        StructCmd = parent.StructCmd;
    }

    if (!NodeType && parent.NodeType) {
        NodeType = parent.NodeType;
    }
    if (SymlinkType == EST_Unset)  { // not set explicitly
        SymlinkType = parent.SymlinkType;
    }
    if (PeerdirType == EPT_Unset)  { // not set explicitly
        PeerdirType = parent.PeerdirType;
    }
    if (!AllExtsAreInputs && InputExts.empty()) { //do not overlap input exts
        AllExtsAreInputs = parent.AllExtsAreInputs;
        CopyParent(parent.InputExts, InputExts);
    }
    if (!AllGlobalExtsAreInputs && GlobalInputExts.empty()) { //do not overlap input exts
        AllGlobalExtsAreInputs = parent.AllExtsAreInputs;
        CopyParent(parent.GlobalInputExts, GlobalInputExts);
    }
    if (UsePeersLateOuts.IsDefaultValue()) {
        UsePeersLateOuts = parent.UsePeersLateOuts;
    }
    if (UseInjectedData.IsDefaultValue()) {
        UseInjectedData = parent.UseInjectedData;
    }
    if (FinalTarget.IsDefaultValue()) {
        FinalTarget = parent.FinalTarget;
    }
    if (ProxyLibrary.IsDefaultValue()) {
        ProxyLibrary = parent.ProxyLibrary;
    }
    if (DepManagementVersionProxy.IsDefaultValue()) {
        DepManagementVersionProxy = parent.DepManagementVersionProxy;
    }
    if (!ParseModuleArgs && parent.ParseModuleArgs) {
        ParseModuleArgs = parent.ParseModuleArgs;
    }
    if (!SetModuleBasename && parent.ParseModuleArgs) {
        SetModuleBasename = parent.SetModuleBasename;
    }
    if (Transition == ETransition::None && parent.Transition != ETransition::None) {
        Transition = parent.Transition;
    }

    for (const auto& i : parent.Allowed) {
        if (!Restricted.contains(i)) {
            Allowed.insert(i);
        }
    }
    for (const auto& i : parent.Restricted) {
        if (!Allowed.contains(i)) {
            Restricted.insert(i);
        }
    }
    for (const auto& i : parent.Ignored) {
        if (!Allowed.contains(i)) {
            Ignored.insert(i);
        }
    }
    for (const auto& i : parent.MacroAliases) {
        MacroAliases.try_emplace(i.first, i.second);
    }

    CopyParent(parent.Globals, Globals);
    SpecServiceVars.insert(SpecServiceVars.end(), parent.SpecServiceVars.begin(), parent.SpecServiceVars.end());
}

void TModuleConf::ApplyOwnerConf(const TModuleConf& owner) {
    // We need rules to apply common properties to submodules of multi-module
    // TODO(spreis)
    for (const auto& i : owner.MacroAliases) {
        MacroAliases.try_emplace(i.first, i.second);
    }
}

void TModuleConf::UniteRestrictions() {
    // Postprocess Allowed/Restricted/Ignored across submodules
    // to allow macros suitable to only some of submodules and ignore these in others

    // Intersect restricted
    THashSet<TStringBuf> allRestricted;
    if (!SubModules.empty()) {
        auto subIter = SubModules.cbegin();
        auto subIterEnd = SubModules.cend();
        allRestricted.insert((*subIter).second->Restricted.begin(), (*subIter).second->Restricted.end());
        for (++subIter; subIter != subIterEnd; ++subIter) {
            const auto& subRest = (*subIter).second->Restricted;
            if (subRest.empty()) {
                allRestricted.clear();
                break;
            }
            EraseNodesIf(allRestricted, [&subRest](const auto& rest) {return !subRest.contains(rest);});
        }
    }

    // If something is not restricted in all submodules ignore it
    for (const auto& sub: SubModules) {
        for (const auto& rest : sub.second->Restricted) {
            if (!allRestricted.contains(rest)) {
                // Ignored has higher priority, so no need to remove from Restricted
                sub.second->Ignored.insert(rest);
            }
        }
    }
}

bool TModuleConf::AddSubmodule(const TString& tag, TModuleConf& sub) {
    auto [_, added] = SubModules.try_emplace(tag, &sub);
    return added;
}

bool TModuleConf::SetProperty(TStringBuf key, TStringBuf name, TStringBuf value, TVars& topVars, bool renderSemantics) {
    if (EqualToOneOf(name, NProperties::RESTRICTED, NProperties::IGNORED, NProperties::ALLOWED, NProperties::GLOBAL)) {
        auto updateCollection = [value, renderSemantics, &topVars] (bool isGlobal, auto& collection) {
            for (const auto arg : StringSplitter(value).Split(' ').SkipEmpty()) {
                collection.insert(TString(arg));
                if (isGlobal) {
                    TString varName = TString::Join(arg, "_GLOBAL");
                    if (!topVars.Has(varName)) {
                        topVars.SetValue(varName, "");
                    }
                    if (renderSemantics) {
                        TString rawVarName = TString::Join(arg, "_GLOBAL_RAW");
                        if (!topVars.Has(rawVarName)) {
                            topVars.SetValue(rawVarName, "");
                        }
                        topVars[rawVarName].IsReservedName = true;
                    }
                    topVars[varName].IsReservedName = true;
                }
            }
        };

        if (name == NProperties::RESTRICTED) {
            updateCollection(false, Restricted);
        } else if (name == NProperties::IGNORED) {
            updateCollection(false, Ignored);
        } else if (name == NProperties::ALLOWED) {
            updateCollection(false, Allowed);
        } else if (name == NProperties::GLOBAL) {
            updateCollection(true, Globals);
        }
    } else if (name == NProperties::CMD) {
        if (!renderSemantics || !HasSemantics) {
            Cmd = value;
        }
    } else if (name == NProperties::SEM) {
        if (renderSemantics) {
            Cmd = value;
            HasSemantics = true;
        }
    } else if (bool global = name == NProperties::GLOBAL_EXTS; name == NProperties::EXTS || global) {
        for (const auto ext : StringSplitter(value).Split(' ').SkipEmpty()) {
            AddExt(ext, global);
        }
    } else if (name == NProperties::GLOBAL_CMD) {
        if (!renderSemantics || !HasSemanticsForGlobals) {
            GlobalCmd = value;
        }
    } else if (name == NProperties::GLOBAL_SEM) {
        if (renderSemantics) {
            GlobalCmd = value;
            HasSemanticsForGlobals = true;
        }
    } else if (name == NProperties::ALIASES) {
        for (const auto alias : StringSplitter(value).Split(' ').SkipEmpty()) {
            TStringBuf from, to;
            Split(alias, "=", from, to);
            auto [_, added] = MacroAliases.try_emplace(from, to);
            if (!added) {
                YErr() << "Duplicate alias for macro " << from << " in module " << Name << " ignored"<< Endl;
            }
        }
    } else if (name == NProperties::NODE_TYPE) {
        if (value == "Library") {
            NodeType = EMNT_Library;
        } else if (value == "Program") {
            NodeType = EMNT_Program;
        } else if (value == "Bundle") {
            NodeType = EMNT_Bundle;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else if (name == NProperties::SYMLINK_POLICY) {
        if (value == "SO") {
            SymlinkType = EST_So;
        } else if (value == "EXE") {
            SymlinkType = EST_Exe;
        } else if (value == "NONE") { //for inherited modules to overlap SO and EXE
            SymlinkType = EST_None;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else if (name == NProperties::PEERDIR_POLICY) {
        if (value == "as_include") {
            PeerdirType = EPT_Include;
        } else if (value == "as_build_from") {
            PeerdirType = EPT_BuildFrom;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else if (name == NProperties::DEFAULT_NAME_GENERATOR) {
        if (value == "DirName") {
            SetModuleBasename = &SetDirNameBasename;
        } else if (value == "UseDirNameOrSetGoPackage") {
            SetModuleBasename = &SetDirNameBasenameOrGoPackage;
        } else if (value == "TwoDirNames") {
            SetModuleBasename = &SetTwoDirNamesBasename;
        } else if (value == "ThreeDirNames") {
            SetModuleBasename = &SetThreeDirNamesBasename;
        } else if (value == "FullPath") {
            SetModuleBasename = &SetFullPathBasename;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else if (name == NProperties::ARGS_PARSER) {
        if (value == "DLL") {
            ParseModuleArgs = &ParseDllModuleArgs;
        } else if (value == "Base") {
            ParseModuleArgs = &ParseBaseModuleArgs;
        } else if (value == "Raw") {
            ParseModuleArgs = &ParseRawModuleArgs;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else if (name == NProperties::USE_INJECTED_DATA) {
        ApplyBoolProperty(UseInjectedData, key, name, value);
        if (UseInjectedData) {
            YDebug() << "Use injected data for " << name << Endl;
        }
    } else if (name == NProperties::USE_PEERS_LATE_OUTS) {
        ApplyBoolProperty(UsePeersLateOuts, key, name, value);
    } else if (name == NProperties::STRUCT_CMD) {
        if (!renderSemantics) {
            ApplyBoolProperty(StructCmd, key, name, value);
            StructCmdSet = true;
        }
    } else if (name == NProperties::STRUCT_SEM) {
        if (renderSemantics) {
            ApplyBoolProperty(StructCmd, key, name, value);
            StructCmdSet = true;
        }
    } else if (name == NProperties::INCLUDE_TAG) {
        ApplyBoolProperty(IncludeTag, key, name, value);
    } else if (name == NProperties::FINAL_TARGET) {
        ApplyBoolProperty(FinalTarget, key, name, value);
    } else if (name == NProperties::PROXY) {
        ApplyBoolProperty(ProxyLibrary, key, name, value);
    } else if (name == NProperties::VERSION_PROXY) {
        ApplyBoolProperty(DepManagementVersionProxy, key, name, value);
    } else if (name == NProperties::PEERDIRSELF) {
        TUniqVector<TString> tags;
        for (const auto tag : StringSplitter(value).Split(' ').SkipEmpty()) {
            tags.Push(TString{tag});
        }
        SelfPeers = tags.Take();
    } else if (name == NProperties::EPILOGUE) {
        Epilogue = Strip(TString(value));
    } else if (name == NProperties::TRANSITION) {
        auto transition = FromString<ETransition>(TString(value));
        if (transition != ETransition::None) {
            Transition = transition;
        } else {
            ReportUnexpectedValueForProperty(key, name, value);
        }
    } else {
        return false;
    }
    return true;
}

void TModuleConf::Load(IInputStream* input) {
    ::Load(input, Cmd);
    ::Load(input, GlobalCmd);
    ::Load(input, Name);
    ::Load(input, Tag);
    ::Load(input, Epilogue);
    ::Load(input, InputExts);
    ::Load(input, GlobalInputExts);
    ::Load(input, AllExtsAreInputs);
    ::Load(input, AllGlobalExtsAreInputs);
    ::Load(input, UseInjectedData);
    ::Load(input, UsePeersLateOuts);
    ::Load(input, IsPackageBundle);
    ::Load(input, IncludeTag);
    ::Load(input, FinalTarget);
    ::Load(input, ProxyLibrary);
    ::Load(input, DepManagementVersionProxy);
    ::Load(input, NodeType);
    ::Load(input, SymlinkType);
    ::Load(input, PeerdirType);
    ::Load(input, HasSemantics);
    ::Load(input, HasSemanticsForGlobals);
    ::Load(input, StructCmd);
    ::Load(input, StructCmdSet);
    ::Load(input, Transition);

    ::Load(input, Restricted);
    ::Load(input, Ignored);
    ::Load(input, Allowed);
    ::Load(input, Globals);
    ::Load(input, MacroAliases);
    ::Load(input, SpecServiceVars);
    // TTaggedModules    SubModules;
    //   Submodules is filled in:
    //     CompleteModules() -> FillInheretedData() -> ApplyOwner() -> AddSubmodule()
    // TOrderedModules   OrderedSubModules;
    //   OrderedSubModules is filled in:
    //     VerifyModuleConf() -> OrderSubModules() ->  OrderSubModulesRecursively()
    ::Load(input, SelfPeers);

    {
        TString str;
        ::Load(input, str);
        ParseModuleArgs = StringToParseModuleArgsFunction(str);
    }

    {
        TString str;
        ::Load(input, str);
        SetModuleBasename = StringToModuleNameFunction(str);
    }
}

void TModuleConf::Save(IOutputStream* output) const {
    ::Save(output, Cmd);
    ::Save(output, GlobalCmd);
    ::Save(output, Name);
    ::Save(output, Tag);
    ::Save(output, Epilogue);
    ::Save(output, InputExts);
    ::Save(output, GlobalInputExts);
    ::Save(output, AllExtsAreInputs);
    ::Save(output, AllGlobalExtsAreInputs);
    ::Save(output, UseInjectedData);
    ::Save(output, UsePeersLateOuts);
    ::Save(output, IsPackageBundle);
    ::Save(output, IncludeTag);
    ::Save(output, FinalTarget);
    ::Save(output, ProxyLibrary);
    ::Save(output, DepManagementVersionProxy);
    ::Save(output, NodeType);
    ::Save(output, SymlinkType);
    ::Save(output, PeerdirType);
    ::Save(output, HasSemantics);
    ::Save(output, HasSemanticsForGlobals);
    ::Save(output, StructCmd);
    ::Save(output, StructCmdSet);
    ::Save(output, Transition);

    ::Save(output, Restricted);
    ::Save(output, Ignored);
    ::Save(output, Allowed);
    ::Save(output, Globals);
    ::Save(output, MacroAliases);
    ::Save(output, SpecServiceVars);
    // TTaggedModules    SubModules;
    //   Submodules is filled in:
    //     CompleteModules() -> FillInheretedData() -> ApplyOwner() -> AddSubmodule()
    // TOrderedModules   OrderedSubModules;
    //   OrderedSubModules is filled in:
    //     VerifyModuleConf() -> OrderSubModules() ->  OrderSubModulesRecursively()
    ::Save(output, SelfPeers);

    ::Save(output, ParseModuleArgsFunctionToString(ParseModuleArgs));
    ::Save(output, ModuleNameFunctionToString(SetModuleBasename));
}

void TBlockData::Inherit(const TString& name, const TBlockData& parent, bool renderSemantics) {
    if (parent.ModuleConf) {
        GetOrInit(ModuleConf).Name = name;
        ModuleConf->Inherit(*parent.ModuleConf, renderSemantics);
    }
    if (parent.CmdProps) {
        GetOrInit(CmdProps).Inherit(*parent.CmdProps);
    }
}

void TBlockData::ApplyOwner(const TString& name, TBlockData& owner) {
    auto& ownerConf = GetOrInit(owner.ModuleConf);
    auto& thisConf = GetOrInit(ModuleConf);

    Y_ENSURE(!thisConf.Tag.empty());
    if (!ownerConf.AddSubmodule(thisConf.Tag, thisConf)) {
        YErr() << name << " has duplicate sub-module tag " << thisConf.Tag << ", ignored" << Endl;
        ModuleConf.Reset();
        return;
    }

    thisConf.ApplyOwnerConf(ownerConf);
}

void TYmakeConfig::ClearYmakeConfig() {
    CommandConf.Clear();
    Conditions.Clear();
    BlockData.clear();
    YmakeConfMD5 = TMd5Sig{};
    YmakeConfWoRulesMD5 = TMd5Sig{};
    CommandDefinitions.Clear();
    ImportedFiles.clear();
    FromCache_ = false;
}

void TYmakeConfig::FillInheritedData(TBlockData& data, const TString& name) {
    if (data.IsMultiModule && !data.Completed) {
        GetOrInit(data.ModuleConf).Name = name;
        data.Completed = true;
        return;
    }

    auto& moduleConf = data.ModuleConf;
    if (const auto& parentName = data.ParentName) {
        if (auto it = BlockData.find(parentName)) {
            auto& [_, parent] = *it;
            if (!parent.Completed) {
                FillInheritedData(parent, parentName);
            }
            data.Inherit(name, parent, RenderSemantics);
        }
    } else if (moduleConf) {
        if (!RenderSemantics) { // commands
            if (moduleConf->Cmd == INHERITED) {
                YConfWarn(Syntax) << INHERITED << " in commands not supported and doesn't have a parent module here" << Endl;
            }
        } else { // semantics
            if (moduleConf->HasSemantics && moduleConf->Cmd == INHERITED) {
                moduleConf->HasSemantics = false; // really has no semantic
                if (!ForeignOnNoSem) {
                    YConfErr(NoSem) << "Module " << name << " doesn't have a parent module to inherit the semantics" << Endl;
                }
            }
            if (moduleConf->HasSemanticsForGlobals && moduleConf->GlobalCmd == INHERITED) {
                moduleConf->HasSemantics = false; // really has no semantic
                if (!ForeignOnNoSem) {
                    YConfErr(NoSem) << "Module " << name << " doesn't have a parent module to inherit the semantics for GLOBAL SRCS" << Endl;
                }
            }
        }
    }

    if (moduleConf) {
        moduleConf->SpecServiceVars.push_back(name);
        if (!moduleConf->SetModuleBasename) {
            moduleConf->SetModuleBasename = SetThreeDirNamesBasename;
        }
        if (!moduleConf->ParseModuleArgs) {
            moduleConf->ParseModuleArgs = ParseBaseModuleArgs;
        }
    }

    if (const auto& ownerName = data.OwnerName) {
        if (auto it = BlockData.find(ownerName)) {
            auto& [_, owner] = *it;
            if (!owner.Completed) {
                GetOrInit(owner.ModuleConf).Name = ownerName;
                owner.Completed = true;
            }
            data.ApplyOwner(name, owner);
        }
    }

    data.Completed = true;
}

void TYmakeConfig::CompleteModules() {
    for (auto& data : BlockData) {
        if (data.second.Completed) {
            continue;
        }
        if (data.second.ParentName.size() && !BlockData.contains(data.second.ParentName)) {
            ythrow TError() << "Macro " << data.first << " was inherited from undefined module " << data.second.ParentName;
        }
        FillInheritedData(data.second, data.first);
    }
    for (auto& data : BlockData) {
        if (data.second.IsMultiModule && data.second.ModuleConf) {
            data.second.ModuleConf.Get()->UniteRestrictions();
        }
    }

    for (const auto& var : CommandConf) {
        if (var.second.IsReservedName) {
            ReservedNames.insert(var.first);
        }
    }
}

TStringBuf TYmakeConfig::GetMacroByExt(const TStringBuf& sectionName, const TStringBuf& fname) const {
    return GetSectionPattern(BlockData, sectionName, NPath::Extension(fname));
}

TStringBuf TYmakeConfig::GetSpecMacroName(const TStringBuf& macroName, const TVector<TStringBuf>& args) const {
    // If the list of actual arguments is empty then this is not our case since we are not able to deduce
    // macro specialization at all
    TStringBuf specMacroName;
    if (!args.empty()) {
        const auto it = BlockData.find(macroName);
        if (it != BlockData.end() && it->second.IsGenericMacro) {
            // We are currently using only the first argument for macro specialization deduction
            specMacroName = GetSectionPattern(it->second, args[0]);
        }
    }
    // Return original macroName if there is no appropriate specialization or macroName is not
    // a generic macro name
    return (specMacroName.empty()) ? macroName : specMacroName;
}

void TYmakeConfig::VerifyModuleConfs() {
    // Verify usual modules first including submodules
    for (auto& data : BlockData) {
        TModuleConf* mod = data.second.ModuleConf.Get();
        if (!mod || data.second.IsMultiModule) {
            continue;
        }
        bool moduleNotDefined = false;
        if (mod->Cmd.empty()) {
            if (RenderSemantics) {
                YWarn() << data.first << " [[alt1]]." << NProperties::SEM << "[[rst]] is not defined" << Endl;
            } else {
                YErr() << data.first << " [[alt1]]." << NProperties::CMD << "[[rst]] is not defined" << Endl;
                moduleNotDefined = true;
            }
        }
        if (!mod->NodeType) {
            YErr() << data.first << " [[alt1]]." << NProperties::NODE_TYPE << "[[rst]] is not defined" << Endl;
            moduleNotDefined = true;
        }
        if (mod->InputExts.empty() && !mod->AllExtsAreInputs) {
            YErr() << data.first << " [[alt1]]." << NProperties::EXTS << "[[rst]] is not defined" << Endl;
            moduleNotDefined = true;
        }
        if (!mod->GlobalCmd.empty() && mod->InputExts.empty() && !mod->AllGlobalExtsAreInputs) {
            YErr() << data.first << " [[alt1]]." << (RenderSemantics ? NProperties::GLOBAL_SEM : NProperties::GLOBAL_CMD) << "[[rst]] is defined but [[alt1]]." << NProperties::GLOBAL_EXTS << "[[rst]] is not defined" << Endl;
            moduleNotDefined = true;
        }
        for (auto i : mod->Restricted) {
            if (mod->Allowed.contains(i)) {
                YErr() << data.first << " macro " << i << " is in allowed list and restricted list" << Endl;
                moduleNotDefined = true;
            }
        }

        if (!mod->Epilogue.empty()) {
            const auto it = BlockData.find(mod->Epilogue);
            if (it == BlockData.end()) {
                YErr() << mod->Epilogue << " macro set in [[alt1]]." << NProperties::EPILOGUE << "[[rst]] property of module " << mod->Name << " does not exist." << Endl;
            } else if (it->second.ModuleConf) {
                YErr() << "The value " << mod->Epilogue << " set in [[alt1]]." << NProperties::EPILOGUE << "[[rst]] property of module " << mod->Name << " should define a macro name." << Endl;
            } else if (it->second.CmdProps && !it->second.CmdProps->ArgNames.empty()) {
                YErr() << "Macro set in [[alt1]]." << NProperties::EPILOGUE << "[[rst]] property of module " << mod->Name << " should be a macro without arguments." << Endl;
            }
        }

        // remove unset state
        if (mod->SymlinkType == EST_Unset) {
            mod->SymlinkType = EST_None;
        }
        // remove unset state
        if (mod->PeerdirType == EPT_Unset) {
            mod->PeerdirType = EPT_BuildFrom;
        }

        if (moduleNotDefined) {
            YErr() << data.first << " undefined!" << Endl;
            if (!data.second.OwnerName.empty()) {
                auto ownIt = BlockData.find(data.second.OwnerName);
                AssertEx(ownIt != BlockData.end(), "owner: " << data.second.OwnerName
                                                             << " is not found by name for: " << data.first);
                ownIt->second.ModuleConf.Get()->SubModules.erase(mod->Tag);
            }
            data.second.ModuleConf.Reset(); //TODO: check if Run mod is OK
        }
    }

    // Verify multimodules now. This is delayed to take into account submodules destruction above
    for (auto& data : BlockData) {
        TModuleConf* mod = data.second.ModuleConf.Get();
        if (!mod || !data.second.IsMultiModule) {
            continue;
        }
        bool moduleNotDefined = false;
        if (mod->SubModules.empty()) {
            YErr() << data.first << " multimodule has no submodules" << Endl;
            moduleNotDefined = true;
        } else {
            const void* parser = nullptr;
            for (auto [_, modConf]: mod->SubModules) {
                if (parser == nullptr) {
                    parser = reinterpret_cast<const void*>(modConf->ParseModuleArgs);
                } else if (parser != reinterpret_cast<const void*>(modConf->ParseModuleArgs)) {
                    YErr() << data.first << " args parsers in submodules don't match" << Endl;
                    moduleNotDefined = true;
                }

                if (!moduleNotDefined) {
                    for (const auto& name : modConf->SelfPeers) {
                        if (!mod->SubModules.contains(name)) {
                            YErr() << "Invalid name of sub-module " << name
                                << " is listed in property .PEERDIRSELF of sub-module "
                                << modConf->Tag << " from multi-module " << data.first << "." << Endl;
                            moduleNotDefined = true;
                        }
                    }
                }
            }
        }

        if (moduleNotDefined) {
            YErr() << data.first << " undefined!" << Endl;
            for (const auto& sub : mod->SubModules) {
                BlockData[sub.second->Name].ModuleConf.Reset();
            }
            data.second.ModuleConf.Reset();
        } else {
            data.second.HasPeerdirSelf = OrderSubModules(data.first, mod->SubModules, &mod->OrderedSubModules);
        }
    }
}

void TYmakeConfig::RegisterResourceLate(const TString& variableName, const TString& value) const {
    // FIXME: Temporary hacky workaround for external resources collection into globals
    TYmakeConfig* mutableThis = const_cast<TYmakeConfig*>(this);
    mutableThis->CommandConf.SetValue(variableName, value);
    mutableThis->ResourceNames.insert(variableName);
}
