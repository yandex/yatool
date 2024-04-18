#include "module_state.h"

#include "args_converter.h"
#include "makefile_loader.h"
#include "macro_string.h"
#include "macro_processor.h"
#include "out.h"
#include "peers.h"
#include "command_store.h"

#include <devtools/ymake/lang/plugin_facade.h>

#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/transitive_requirements_check.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/manager.h>

#include <util/folder/path.h>
#include <util/string/cast.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/string/vector.h>

namespace {
    const TStringBuf EMPTY_TAG_NAME = TStringBuf("__EMPTY__");
    const TStringBuf PROP_TAG = TStringBuf("TAG");
    const TStringBuf PROP_FILENAME = TStringBuf("FILENAME");
    const TStringBuf PROP_BASENAME = TStringBuf("BASENAME");
    const TStringBuf PROP_PROVIDES = TStringBuf("PROVIDES");
    const TStringBuf VAR_DYNAMIC_LINK = TStringBuf("DYNAMIC_LINK");
    const TStringBuf VAR_HAS_MANAGEABLE_PEERS = TStringBuf("HAS_MANAGEABLE_PEERS");
    const TStringBuf VAR_CONSUME_NON_MANAGEABLE_PEERS = TStringBuf("CONSUME_NON_MANAGEABLE_PEERS");
    const TStringBuf VAR_DONT_RESOLVE_INCLUDES = TStringBuf("DONT_RESOLVE_INCLUDES");
    const TStringBuf VAR_CHECK_INTERNAL = TStringBuf("CHECK_INTERNAL");
    const TStringBuf VAR_INTERNAL_EXCEPTIONS = TStringBuf("INTERNAL_EXCEPTIONS");
    const TStringBuf VAR_USE_GLOBAL_CMD = TStringBuf("USE_GLOBAL_CMD");
    const TStringBuf VAR_GO_TEST_FOR_DIR = TStringBuf("GO_TEST_FOR_DIR");
    const TStringBuf VAR_START_TARGET = TStringBuf("START_TARGET");
    const TStringBuf VAR_GO_HAS_INTERNAL_TESTS = TStringBuf("GO_HAS_INTERNAL_TESTS");
    const TStringBuf VAR_PASS_PEERS = TStringBuf("PASS_PEERS");
    const TStringBuf VAR_IGNORE_DUPSRC = TStringBuf("_IGNORE_DUPSRC");
    const TStringBuf CONFIG_VAR_NAMES[] = {
        VAR_PEERDIR_TAGS,
        MANGLED_MODULE_TYPE,
        VAR_MODULE_LANG,
        VAR_MODULE_TYPE,
        VAR_PASS_PEERS,
        VAR_GO_TEST_FOR_DIR,
        TStringBuf("PROTO_HEADER_EXTS"),
        TStringBuf("EV_HEADER_EXTS"),
        TStringBuf("DEPENDENCY_MANAGEMENT_VALUE"),
        TStringBuf("EXCLUDE_VALUE"),
        TStringBuf("IGNORE_JAVA_DEPENDENCIES_CONFIGURATION"),
        TStringBuf("JAVA_DEPENDENCIES_CONFIGURATION_VALUE"),
        TStringBuf("RUN_JAVA_PROGRAM_VALUE"),
        TStringBuf("TEST_CLASSPATH_VALUE"),
        TStringBuf("NON_NAMAGEABLE_PEERS"),
        TStringBuf("DART_CLASSPATH_DEPS"),
        TStringBuf("DART_CLASSPATH"),
        TStringBuf("UNITTEST_DIR"),
        TStringBuf("TS_CONFIG_ROOT_DIR"),
        TStringBuf("TS_CONFIG_OUT_DIR"),
        TStringBuf("TS_CONFIG_SOURCE_MAP"),
        TStringBuf("TS_CONFIG_DECLARATION"),
        TStringBuf("TS_CONFIG_DECLARATION_MAP"),
        TStringBuf("TS_CONFIG_DEDUCE_OUT"),
        TStringBuf("TS_CONFIG_PRESERVE_JSX"),
    };
    const TStringBuf DEFAULT_VAR_NAMES[] = {
        "CMAKE_CURRENT_SOURCE_DIR"sv,
        "CMAKE_CURRENT_BINARY_DIR"sv,
        "ARCADIA_BUILD_ROOT"sv,
        "ARCADIA_ROOT"sv,
        "BINDIR"sv,
        "CURDIR"sv,
        "PYTHON_BIN"sv,
        "TEST_CASE_ROOT"sv,
        "TEST_OUT_ROOT"sv,
        "TEST_SOURCE_ROOT"sv,
        "TEST_WORK_ROOT"sv
    };

    const TStringBuf INTERNAL_NAME = TStringBuf("internal");
    const TStringBuf VAR_USE_ALL_SRCS = TStringBuf(".USE_ALL_SRCS");

    const char* YesNo(bool val) noexcept {
        return val ? "yes" : "no";
    }

    ERenderModuleType ComputeRenderModuleType(const TModule* mod, EMakeNodeType nodeType, ESymlinkType symlinkType) {
        Y_ASSERT(mod != nullptr);
        Y_ASSERT(IsModuleType(nodeType));

        ERenderModuleType moduleType = ERenderModuleType::Bundle;
        if (nodeType == EMNT_Bundle) {
            const auto moduleTypeVarValue = mod->Get(VAR_MODULE_TYPE);
            if (moduleTypeVarValue == "PROGRAM"sv) {
                moduleType = ERenderModuleType::Program;
            } else if (moduleTypeVarValue == "DLL"sv) {
                moduleType = ERenderModuleType::Dll;
            } else if (moduleTypeVarValue == "LIBRARY"sv) {
                moduleType = ERenderModuleType::Library;
            } else {
                moduleType = ERenderModuleType::Bundle;
            }
        } else if (nodeType == EMNT_Library) {
            if (symlinkType == EST_So) {
                moduleType = ERenderModuleType::Dll;
            } else {
                moduleType = ERenderModuleType::Library;
            }
        } else if (nodeType == EMNT_Program) {
            moduleType = ERenderModuleType::Program;
        }

        return moduleType;
    }
}

void InitModuleVars(TVars& vars, TVars& commandConf, ui32 makeFileId, TFileView moduleDir) {
    vars.Base = &commandConf;
    vars.Id = makeFileId;

    vars.SetValue("CMAKE_CURRENT_SOURCE_DIR", TString::Join("${ARCADIA_ROOT}", NPath::PATH_SEP_S, moduleDir.CutType()));       // temp
    vars.SetValue("CMAKE_CURRENT_BINARY_DIR", TString::Join("${ARCADIA_BUILD_ROOT}", NPath::PATH_SEP_S, moduleDir.CutType())); // temp
    vars["ARCADIA_BUILD_ROOT"].DontExpand = true;
    vars["ARCADIA_ROOT"].DontExpand = true;
    vars["BINDIR"].DontExpand = true;
    vars["CURDIR"].DontExpand = true;
    vars["PYTHON_BIN"].DontExpand = true;
    vars["TEST_CASE_ROOT"].DontExpand = true;
    vars["TEST_OUT_ROOT"].DontExpand = true;
    vars["TEST_SOURCE_ROOT"].DontExpand = true;
    vars["TEST_WORK_ROOT"].DontExpand = true;
}

TModuleSavedState::TModuleSavedState(const TModule& mod) {
    mod.Save(*this);
}

TModule::TModule(TModuleSavedState&& saved, TModulesSharedContext& context)
    : IncDirs(context.SymbolsTable)
    , NodeType(saved.NodeType)
    , Id(saved.Id)
    , GlobalLibId(saved.GlobalLibId)
    , PeerdirType(saved.PeerdirType)
    , PeersRules(context.PeersRules, std::move(saved.PeersRules))
    , Attrs(saved.Attrs)
    , Symbols(context.SymbolsTable)
    , SharedEntries(context.SharedEntries[saved.MakefileId])
{
    DirId = saved.DirId;
    MakefileId = saved.MakefileId;
    GhostPeers = std::move(saved.GhostPeers);
    Y_ASSERT(Id != 0 && NodeType != EMNT_Deleted && DirId != 0);
    TFileView moduleDir = GetDir();

    InitModuleVars(Vars, context.CommandConf, MakefileId, moduleDir);
    for (ui32 varId : saved.ConfigVars) {
        TStringBuf prop = Symbols.CmdNameById(varId).GetStr();
        TStringBuf propName = GetPropertyName(prop);
        TStringBuf propValue = GetPropertyValue(prop);
        if (propName == PROP_TAG) {
            Tag = propValue;
        } else if (propName == PROP_FILENAME) {
            SetFileName(propValue);
        } else if (propName == PROP_BASENAME) {
            BaseName = propValue;
        } else if (propName == PROP_PROVIDES) {
            Provides = StringSplitter(propValue).Split(' ');
        } else {
            Set(propName, propValue);
        }
    }

    if (GlobalLibId != 0) {
        GlobalName = Symbols.FileNameById(GlobalLibId);
    }

    SetupPeerdirRestrictions();

    for (const auto entry: saved.OwnEntries) {
        AddEntry(entry);
    }

    SrcDirs.RestoreFromsIds(saved.SrcsDirsIds, Symbols);
    IncDirs.Load(saved.IncDirs);

    if (moduleDir.IsValid()) {
        SrcDirs.Push(moduleDir);
    }

    if (!saved.MissingDirsIds.empty()) {
        MissingDirs = MakeHolder<TDirs>();
        MissingDirs->RestoreFromsIds(saved.MissingDirsIds, Symbols);
    }

    if (!saved.DataPathsIds.empty()) {
        DataPaths = MakeHolder<TDirs>();
        DataPaths->RestoreFromsIds(saved.DataPathsIds, Symbols);
    }

    SelfPeers = saved.SelfPeers;
    ExtraOuts = saved.ExtraOuts;

    ResolveResults = std::move(saved.ResolveResults);

    RawIncludes = std::move(saved.RawIncludes);

    Loaded = true;
}

void TModule::Save(TModuleSavedState& saved) const {
    saved.Id = Id;
    saved.NodeType = NodeType;
    saved.PeerdirType = PeerdirType;
    saved.DirId = DirId;
    saved.MakefileId = MakefileId;
    saved.GhostPeers = GhostPeers;
    saved.GlobalLibId = GlobalLibId;
    saved.SelfPeers = SelfPeers;
    saved.ExtraOuts = ExtraOuts;

    saved.Attrs.AllBits = Attrs.AllBits;

    PeersRules.Save(saved.PeersRules);

    if (!Tag.empty()) {
        saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(PROP_TAG, Tag)));
    }
    saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(PROP_FILENAME, FileName)));
    saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(PROP_BASENAME, BaseName)));
    if (!Provides.empty()) {
        saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(PROP_PROVIDES, JoinStrings(Provides.begin(), Provides.end(), TStringBuf(" ")))));
    }
    for (const auto& name : CONFIG_VAR_NAMES) {
        TStringBuf value = Get(name);
        if (!value.empty()) {
            saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(name, value)));
        }
    }

    for (auto item: TRANSITIVE_CHECK_REGISTRY) {
        for (auto name: item.ConfVars) {
            TStringBuf value = Get(name);
            if (!value.empty()) {
                saved.ConfigVars.push_back(Symbols.AddName(EMNT_Property, FormatProperty(name, value)));
            }
        }
    }

    saved.OwnEntries = GetOwnEntries().Data();

    saved.SrcsDirsIds = SrcDirs.SaveAsIds();

    IncDirs.Save(saved.IncDirs);

    if (MissingDirs) {
        saved.MissingDirsIds = MissingDirs->SaveAsIds();
    }
    if (DataPaths) {
        saved.DataPathsIds = DataPaths->SaveAsIds();
    }

    saved.ResolveResults = ResolveResults;

    saved.RawIncludes = RawIncludes;
}

TModule::TModule(TFileView dir, TStringBuf makefile, TStringBuf tag, TModulesSharedContext& context)
    : IncDirs(context.SymbolsTable)
    , NodeType(EMNT_Deleted)
    , DirId(dir.GetElemId())
    , MakefileId(context.SymbolsTable.AddName(EMNT_MakeFile, makefile))
    , Tag(tag)
    , PeersRules(context.PeersRules, dir.GetTargetStr())
    , Symbols(context.SymbolsTable)
    , SharedEntries(context.SharedEntries[MakefileId])
{
    if (dir.IsValid()) {
        SrcDirs.Push(dir);
    }
}

void TModule::Init(TString fileName, TString globalFileName, TString baseName, const TModuleConf& conf) {
    Y_ASSERT(!Committed);
    Y_ASSERT(FileName.empty() && BaseName.empty());
    Y_ASSERT(fileName.Contains(baseName));
    BaseName = baseName;
    SetFileName(fileName);
    GlobalFileName = globalFileName;

    NodeType = conf.NodeType;
    PeerdirType = conf.PeerdirType;

    Attrs.UseInjectedData = conf.UseInjectedData;
    Attrs.UsePeers = PeerdirType == EPT_BuildFrom;
    Attrs.UsePeersLateOuts = conf.UsePeersLateOuts;
    Attrs.FinalTarget = conf.FinalTarget;
    Attrs.ProxyLibrary = conf.ProxyLibrary;
    Attrs.RenderModuleType = static_cast<ui32>(ComputeRenderModuleType(this, NodeType, conf.SymlinkType));
}

void TModule::SetDirsComplete() {
    Y_ASSERT(!IsDirsComplete());
    IncludesComplete = true;
}

void TModule::SetPeersComplete() {
    Y_ASSERT(!IsPeersComplete());
    PeersComplete = true;
}

void TModule::SetToolsComplete() noexcept {
    Y_ASSERT(!IsToolsComplete());
    ToolsComplete = true;
}

void TModule::FinalizeConfig(ui32 id, const TModuleConf& conf) {
    AssertEx(!HasId(), "Module already has ID");
    AssertEx(id != 0 && id != BAD_MODULE, "Invalid ID for module");
    Y_ASSERT(!Committed);

    Id = id;

    Attrs.RequireDepManagement = Get(VAR_HAS_MANAGEABLE_PEERS) == "yes";
    Attrs.ConsumeNonManageablePeers = Get(VAR_CONSUME_NON_MANAGEABLE_PEERS) == "yes";
    Attrs.DynamicLink = Get(VAR_DYNAMIC_LINK) == "yes";
    Attrs.DontResolveIncludes = Vars.Contains(VAR_DONT_RESOLVE_INCLUDES) && Get(VAR_DONT_RESOLVE_INCLUDES) != "no";
    Attrs.UseGlobalCmd = Get(VAR_USE_GLOBAL_CMD) != "no" && !conf.GlobalCmd.empty();
    Attrs.NeedGoDepsCheck = Vars.Contains(VAR_GO_TEST_FOR_DIR) && Get(VAR_GO_HAS_INTERNAL_TESTS) == "yes";
    Attrs.IsStartTarget = Get(VAR_START_TARGET) != "no"sv;
    Attrs.IgnoreDupSrc = Get(VAR_IGNORE_DUPSRC) == "yes"sv;
    if (Vars.Contains(VAR_PASS_PEERS)) {
        Attrs.PassPeers = IsTrue(Vars.EvalValue(VAR_PASS_PEERS));
    } else {
        Attrs.PassPeers = GetNodeType() == EMNT_Bundle || IsStaticLib();
    }
    if (Vars.Contains(VAR_MODULE_TAG)) {
        Tag = Get(VAR_MODULE_TAG);
    } else {
        AssertEx(!Attrs.FromMultimodule, TString("Variable ") + TString{VAR_MODULE_TAG} + " is not set for sub-module of multimodule");
    }
    Attrs.UseAllSrcs = Get(VAR_USE_ALL_SRCS) == "yes";
    SetupPeerdirRestrictions();

    if (conf.HasSemantics && !conf.CmdIgnore.empty()) {
        auto dummyCommandStore = TCommands();
        auto dummyCmdInfo = TCommandInfo(nullptr, nullptr, nullptr);
        auto compiled = dummyCommandStore.Compile(conf.CmdIgnore, Vars, Vars, false, EOutputAccountingMode::Module);
        auto ignore = TCommands::SimpleCommandSequenceWriter()
            .Write(dummyCommandStore, compiled.Expression, Vars, {}, dummyCmdInfo, nullptr)
            .Extract();
        if (ignore.size() == 1 && ignore[0].size() == 1 && IsTrue(ignore[0][0]))
            SetSemIgnore();
    }

    NotifyInitComplete();
}

TFileView TModule::GetName() const {
    Y_ASSERT(Name.IsValid());
    return Name;
}

TStringBuf TModule::Get(const TStringBuf& name) const {
    return GetCmdValue(Vars.Get1(name));
}

void TModule::Set(const TStringBuf& name, const TStringBuf& value) {
    Vars.SetValue(name, value);
}

bool TModule::Enabled(const TStringBuf& name) const {
    return Vars.IsTrue(name);
}

const TString& TModule::UnitName() const {
    return BaseName;
}

const TString& TModule::UnitFileName() const {
    return FileName;
}

TStringBuf TModule::UnitPath() const {
    return GetDir().GetTargetStr();
}

void TModule::GetModuleDirs(const TStringBuf& dirType, TVector<TString>& dirs) const {
    if (dirType == TStringBuf("SRCDIRS")) {
        SrcDirs.IntoStrings(dirs);
    } else if (dirType == TStringBuf("PEERDIRS")) {
        Peers.IntoStrings(dirs);
    } else {
        YConfErr(BadInput) << "unknown dirType: " << dirType << ". Available: SRCDIRS, PEERDIRS." << Endl;
    }
}

TVars TModule::ModuleDirsToVars() const {
    Y_ASSERT(IsDirsComplete());
    TVars vars;
    TVector<TStringBuf> srcDirs, peers;
    SrcDirs.IntoStrings(srcDirs);
    Peers.IntoStrings(peers);
    vars.SetAppend("SRCDIR", srcDirs);
    vars.SetAppend("PEERDIR", peers);
    return vars;
}

TString TModule::GetUserType() const {
    TStringBuf type = Get(MANGLED_MODULE_TYPE);
    if (!Attrs.FromMultimodule) {
        return TString{type};
    }
    const size_t pos = type.rfind(MODULE_MANGLING_DELIM);
    if (Y_UNLIKELY(pos == type.npos)) {
        return TString{type};
    }
    TString res;
    res.reserve(type.size() - MODULE_MANGLING_DELIM.size() + 2);
    return TString{type.SubStr(pos + MODULE_MANGLING_DELIM.size())} + "[" + type.SubStr(0, pos) + "]";
}

EPeerSearchStatus TModule::MatchPeer(const TModule& peer, const TMatchPeerRequest& request) const {
    return PeersRestrictions.Match(peer, request);
}

void TModule::SetupPeerdirRestrictions() {
    ImportPeerdirTags();
    ImportPeerdirRules();
    ImportPeerdirPolicy();
    AddInternalRule();
}

void TModule::ImportPeerdirTags() {
    THashSet<TString> peerdirTags;

    if (Vars.Contains(VAR_PEERDIR_TAGS)) {
        TStringBuf allTags = Get(VAR_PEERDIR_TAGS);
        StringSplitter(allTags).Split(' ').AddTo(&peerdirTags);
    }
    if (peerdirTags.contains(EMPTY_TAG_NAME)) {
        peerdirTags.insert(TString());
    }

    auto match = [peerdirTags = std::move(peerdirTags), this](const TModule& peer) {
        return !((GetTag().empty() && peer.GetTag().empty()) || peerdirTags.contains(peer.GetTag()));
    };

    PeersRestrictions.Add({EPeerSearchStatus::DeprecatedByTags, std::move(match)});
}

void TModule::ImportPeerdirRules() {
    auto match = [this](const TModule& peer) {
        return PeersRules(peer.PeersRules) != TPeersRules::EPolicy::ALLOW;
    };

    PeersRestrictions.Add({EPeerSearchStatus::DeprecatedByRules, std::move(match)});
}

void TModule::ImportPeerdirPolicy() {
    Attrs.UsePeers = PeerdirType == EPT_BuildFrom;

    if (NodeType != EMNT_Bundle && !IsProxyLibrary()) {
        auto match = [](const TModule& peer) {
            return !(peer.IsStaticLib() || peer.IsProxyLibrary());
        };

        PeersRestrictions.Add({EPeerSearchStatus::DeprecatedByFilter, std::move(match)});
    }
}

void TModule::AddInternalRule() {
    TStringBuf moduleDir = Symbols.FileNameById(DirId).GetTargetStr();

    bool isInternal = moduleDir.Contains(INTERNAL_NAME);
    Attrs.IsInternal = IsGoModule() && isInternal;

    if (isInternal && !Attrs.IsInternal && Vars.Base != nullptr && Vars.Base->IsTrue(VAR_CHECK_INTERNAL)) {
        Attrs.IsInternal = true;
        for (const auto& ex: StringSplitter(GetCmdValue(Vars.Base->Get1(VAR_INTERNAL_EXCEPTIONS))).Split(' ').SkipEmpty()) {
             if (moduleDir.Contains(ex.Token())) {
                  Attrs.IsInternal = false;
                  break;
             }
        }
    }

    auto internalMatch = [moduleDir](const TModule& peer) {
        if (!peer.GetAttrs().IsInternal) {
            return false;
        }

        TPathSplitUnix parent(moduleDir);
        TPathSplitUnix child(peer.GetDir().GetTargetStr());
        size_t prefixSize = 0;
        while (parent.size() > prefixSize && child.size() > prefixSize && parent[prefixSize] == child[prefixSize]) {
            prefixSize++;
        }

        prefixSize++;

        while (child.size() > prefixSize) {
            if (child[prefixSize] == INTERNAL_NAME) {
                return true;
            }

            prefixSize++;
        }

        return false;
    };

    PeersRestrictions.Add({EPeerSearchStatus::DeprecatedByInternal, std::move(internalMatch)});
}

bool TModule::AddEntry(ui32 id) {
    auto added = GetOwnEntries().Push(id);
    if (IsFromMultimodule()) {
        GetSharedEntries().Push(id);
    }
    return added;
}

TOwnEntries& TModule::GetSharedEntries() const {
    if (!SharedEntries) {
        SharedEntries = MakeHolder<TOwnEntries>();
    }
    return *SharedEntries;
}

TOwnEntries& TModule::GetOwnEntries() const {
    if (!IsFromMultimodule()) {
        return GetSharedEntries();
    }
    if (!OwnEntries) {
        OwnEntries = MakeHolder<TOwnEntries>();
    }
    return *OwnEntries;
}


void TModule::TrimVars() {
    TVars newVars;
    const constexpr size_t cfgNamesSize = sizeof(CONFIG_VAR_NAMES)/sizeof(*CONFIG_VAR_NAMES);
    const constexpr size_t defNamesSize = sizeof(DEFAULT_VAR_NAMES)/sizeof(*DEFAULT_VAR_NAMES);
    size_t tansNamesSize = 0;
    for (auto item: TRANSITIVE_CHECK_REGISTRY) {
        tansNamesSize += item.ConfVars.size();
    }

    newVars.reserve(cfgNamesSize + defNamesSize + tansNamesSize);
    newVars.Base = Vars.Base;
    newVars.Id = Vars.Id;
    for (const auto& name : DEFAULT_VAR_NAMES) {
        if (auto it = Vars.find(name); it != Vars.end())
            newVars[name] = std::move(it->second);
    }
    for (const auto& name : CONFIG_VAR_NAMES) {
        if (auto it = Vars.find(name); it != Vars.end())
            newVars[name] = std::move(it->second);
    }
    for (auto item: TRANSITIVE_CHECK_REGISTRY) {
        for (auto name: item.ConfVars) {
            if (auto it = Vars.find(name); it != Vars.end())
                newVars[name] = std::move(it->second);
        }
    }
    std::swap(Vars, newVars);
}

void DumpModuleInfo(IOutputStream& out, const TModule& module) {
    out << module.GetName() << Endl;
    out << "\tModule Dir: " << module.GetDir() << Endl;
    out << "\tIs loaded: " << YesNo(module.IsLoaded()) << Endl;
    out << "\tIs final target: " << YesNo(module.GetAttrs().FinalTarget) << Endl;
    out << "\tFrom multimodule: " << YesNo(module.GetAttrs().FromMultimodule) << Endl;
    out << "\tIs proxy library: " << YesNo(module.GetAttrs().ProxyLibrary) << Endl;
    out << "\tIs ignore sem: " << YesNo(module.GetAttrs().SemIgnore) << Endl;
    out << "\tNodeType: " << module.GetNodeType() << Endl;
    out << "\tPeerdirType: " << module.GetPeerdirType() << Endl;

    out << "\tSrcDirs: " << JoinStrings(module.SrcDirs.begin(), module.SrcDirs.end(), " ") << Endl;
    out << "\tPeers: " << JoinStrings(module.Peers.begin(), module.Peers.end(), " ") << Endl;
    out << "\tMissingDirs: " << (module.MissingDirs ? JoinStrings(module.MissingDirs->begin(), module.MissingDirs->end(), " ") : TString("<empty>")) << Endl;
    out << "\tDataPaths: " << (module.DataPaths ? JoinStrings(module.DataPaths->begin(), module.DataPaths->end(), " ") : TString("<empty>")) << Endl;

    module.IncDirs.Dump(out);

    for (TStringBuf confVar: CONFIG_VAR_NAMES) {
        const auto* var = module.Vars.Lookup(confVar);
        if (!var) {
            continue;
        }
        out << "\tVar " << confVar << ": ";
        bool needSepartor = false;
        for (const auto& varStr : *var) {
            out << GetPropertyValue(varStr.Name) << (std::exchange(needSepartor, true) ? " " : "");
        }
        out << Endl;
    }
    for (auto item: TRANSITIVE_CHECK_REGISTRY) {
        for (auto name: item.ConfVars) {
            const auto* var = module.Vars.Lookup(name);
            if (!var) {
                continue;
            }
            out << "\tVar " << name << ": ";
            bool needSepartor = false;
            for (const auto& varStr : *var) {
                out << GetPropertyValue(varStr.Name) << (std::exchange(needSepartor, true) ? " " : "");
            }
            out << Endl;
        }
    }
}

void DumpModuleInfoJson(NJsonWriter::TBuf& json, const TModule& module) {
    json.BeginObject();

    json.WriteKey("name");
    json.WriteString(module.GetName().GetTargetStr());
    json.WriteKey("module-dir");
    json.WriteString(module.GetDir().GetTargetStr());
    json.WriteKey("is-loaded");
    json.WriteBool(module.IsLoaded());
    json.WriteKey("is-final-target");
    json.WriteBool(module.GetAttrs().FinalTarget);
    json.WriteKey("from-multimodule");
    json.WriteBool(module.GetAttrs().FromMultimodule);
    json.WriteKey("is-proxy-library");
    json.WriteBool(module.GetAttrs().ProxyLibrary);

    json.WriteKey("node-type");
    TStringStream nts;
    nts << module.GetNodeType();
    TString nt(nts.Str());
    json.WriteString(nt);
    json.WriteKey("peerdir-type");
    TStringStream pts;
    pts << module.GetPeerdirType();
    TString pt(pts.Str());
    json.WriteString(pt);

    json.WriteKey("src-dirs");
    json.BeginList();
    for (const auto dir : module.SrcDirs) {
        json.WriteString(dir.GetTargetStr());
    }
    json.EndList();

    json.WriteKey("peers");
    json.BeginList();
    for (const auto peer : module.Peers) {
        json.WriteString(peer.GetTargetStr());
    }
    json.EndList();

    json.WriteKey("missing-dirs");
    if (module.MissingDirs) {
        json.BeginList();
        for (const auto dir : *module.MissingDirs) {
            json.WriteString(dir.GetTargetStr());
        }
        json.EndList();
    } else {
        json.WriteNull();
    }

    json.WriteKey("data-paths");
    if (module.DataPaths) {
        json.BeginList();
        for (const auto dir : *module.DataPaths) {
            json.WriteString(dir.GetTargetStr());
        }
        json.EndList();
    } else {
        json.WriteNull();
    }

    module.IncDirs.DumpJson(json);

    json.WriteKey("vars");
    json.BeginObject();
    for (TStringBuf confVar: CONFIG_VAR_NAMES) {
        const auto* var = module.Vars.Lookup(confVar);
        if (!var) {
            continue;
        }
        json.WriteKey(confVar);
        json.BeginList();
        for (const auto& varStr : *var) {
            json.WriteString(GetPropertyValue(varStr.Name));
        }
        json.EndList();
    }
    for (auto item: TRANSITIVE_CHECK_REGISTRY) {
        for (auto name: item.ConfVars) {
            const auto* var = module.Vars.Lookup(name);
            if (!var) {
                continue;
            }
            json.WriteKey(name);
            json.BeginList();
            for (const auto& varStr : *var) {
                json.WriteString(GetPropertyValue(varStr.Name));
            }
            json.EndList();
        }
    }
    json.EndObject();

    json.EndObject();
}
