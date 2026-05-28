#pragma once

#include "vars.h"
#include "conf.h"
#include "dirs.h"
#include "peers.h"
#include "addincls.h"

#include <devtools/ymake/compact_graph/dep_types.h>

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/transitive_constraints.h>
#include <devtools/ymake/vardefs.h>

#include <library/cpp/json/writer/json.h>

#include <util/generic/deque.h>
#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/hash_set.h>
#include <util/generic/set.h>
#include <util/ysaveload.h>

class TSymbols;
class TModule;
class TAppliedPeersRules;
class TBuildConfiguration;

enum class EPeerSearchStatus;
struct TModuleConf;

// Diag reporter for NPath::ConstructYDir
const auto ConstrYDirDiag = [](NPath::EDirConstructIssue issue, const TStringBuf& path) {
    switch (issue) {
        case NPath::EDirConstructIssue::ExtraSep:
            YConfWarn(Style) << "extra " << NPath::PATH_SEP << " in " << path << Endl;
            break;
        case NPath::EDirConstructIssue::TrailingSep:
            YConfErr(Style) << path << " has trailing " << NPath::PATH_SEP << Endl;
            break;
        case NPath::EDirConstructIssue::SourceDir:
        case NPath::EDirConstructIssue::EmptyDir:
            break; // Do nothing
    }
};

enum class EGhostType {
    Material,
    Virtual
};

enum class ERenderModuleType {
    Bundle /* "bundle" */,
    Library /* "lib" */,
    Program /* "bin" */,
    Dll /* "so" */,
};

void InitModuleVars(TVars& vars, TVars& commandConf, TFileElemId makeFileId, TFileView moduleDir);

union TModuleAttrs {
    ui32 AllBits = 0;
    struct { // 24 bits used
        ui32 DontResolveIncludes : 1;  // Avoid includes resolution in any form
        ui32 FromMultimodule     : 1;  // This module is created from multimodule
        ui32 UsePeers            : 1;
        ui32 PassPeers           : 1;
        ui32 IsInternal          : 1;  // ../internal/..
        ui32 Fake                : 1;  // fake_src was added
        ui32 FinalTarget         : 1;  // Programs and alike
        ui32 ProxyLibrary        : 1;  // For Proxy Libraries

        ui32 CheckProvides       : 1;  // Require to check PEER feature conflicts
        ui32 RequireDepManagement : 1;  // Require to run dependency management on module peers
        ui32 ConsumeNonManageablePeers: 1; // Module with RequireDepManagement adds peers without PropagateDepManagement to MANAGED_PEERS_CLOSURE
        ui32 DepManagementVersionProxy: 1; // Module is used as proxy without version which must be replaced by real module with exact version during dependency management
        ui32 DynamicLink         : 1; // PEERDIR to this module is interpreted as dynamically linked dependency (used by license checks)
        ui32 UseGlobalCmd        : 1; // Use GLOBAL_CMD
        ui32 NeedGoDepsCheck     : 1; // Go internal test
        ui32 IsStartTarget       : 1; // Use this module as start target

        ui32 UsePeersLateOuts    : 1; // module uses PEERS_LATE_OUTS var
        ui32 RenderModuleType    : 2; // module type for target_properties
        ui32 SemIgnore           : 1; // module is ignored in sem graph
        ui32 IgnoreDupSrc        : 1; // ignore DupSrc errors for REPORT_ALL_DUPSRC
        ui32 UseAllSrcs          : 1; // module uses ALL_SRCS variable
        ui32 SemForeign          : 1; // Foreign target for sem graph
        ui32 DepManagementTransparent : 1; // DEPENDENCY_MANAGEMENT_TRANSPARENT: closure propagation without local DEPENDENCY_MANAGEMENT/EXCLUDE rules
    };
};

static_assert(sizeof(TModuleAttrs::AllBits) == sizeof(TModuleAttrs));

using TSharedEntriesMap = THashMap<TFileElemId, THolder<TOwnEntries>>;

struct TModulesSharedContext {
    TSharedEntriesMap& SharedEntries;
    TSymbols& SymbolsTable;
    TVars& CommandConf;
    const TPeersRules& PeersRules;
};

struct TResolveResult {
    TFileElemId OrigPath;
    TFileElemId ResolveDir;
    TFileElemId ResultPath;

    static constexpr TFileElemId EmptyPath = TFileElemId(std::numeric_limits<ui32>::max());

    bool operator== (const TResolveResult& rhs) const noexcept {
        return
            OrigPath == rhs.OrigPath &&
            ResolveDir == rhs.ResolveDir &&
            ResultPath == rhs.ResultPath;
    }

    bool operator!= (const TResolveResult& rhs) const noexcept {
        return !(*this == rhs);
    }

    Y_SAVELOAD_DEFINE(OrigPath, ResolveDir, ResultPath);
};

template<>
struct THash<TResolveResult>: THash<std::tuple<ui32, ui32, ui32>> {
    const THash<std::tuple<ui32, ui32, ui32>>& TupleHasher() const noexcept {return *this;}

    size_t operator()(const TResolveResult& value) const {
        return TupleHasher()(std::make_tuple(RawElemId(value.OrigPath), RawElemId(value.ResolveDir), RawElemId(value.ResultPath)));
    }
};

using TRawIncludesInfo = THashMap<TPropertyType, TUniqVector<TDepsCacheId>>;
using TRawIncludes = THashMap<TFileElemId, TRawIncludesInfo>;

class TModuleSavedState {
private:
    friend class TModule;

    TFileElemId Id = TFileElemId();
    TFileElemId DirId = TFileElemId();
    TFileElemId MakefileId = TFileElemId();
    TFileElemId GlobalLibId = TFileElemId();

    EMakeNodeType NodeType = EMNT_Deleted;

    EPeerdirType PeerdirType = EPT_Unset;

    TModuleAttrs Attrs;

    // BaseName=REALPRJNAME
    // FileName=PREFIX+REALPRJNAME+SUFFIX
    // Tag=
    // PeerdirTags=
    // ConfigVars ()
    TVector<TCmdElemId> ConfigVars;

    TModuleIncDirs::TSavedState IncDirs;
    TVector<TFileElemId> SrcsDirsIds;
    TVector<TFileElemId> MissingDirsIds;
    TVector<TFileElemId> DataPathsIds;
    TVector<ui32> SelfPeers;
    TVector<TFileElemId> ExtraOuts;

    TVector<ui32> OwnEntries;

    THashSet<TResolveResult> ResolveResults;

    TRawIncludes RawIncludes;

    THashMap<TFileElemId, EGhostType> GhostPeers;

    TPeersRulesSavedState PeersRules;

    ETransition Transition;

    TModuleGlobsData ModuleGlobsData;

    THashSet<TFileElemId> QueriedPeers;

public:
    TModuleSavedState(const TModule& mod);

    TModuleSavedState() = default;

    TModuleSavedState(TModuleSavedState&&) noexcept = default;

    Y_SAVELOAD_DEFINE(
        Id,
        NodeType,
        DirId,
        MakefileId,
        GlobalLibId,
        PeerdirType,
        Attrs.AllBits,
        ConfigVars,
        SrcsDirsIds,
        OwnEntries,
        ResolveResults,
        RawIncludes,
        GhostPeers,
        IncDirs,
        MissingDirsIds,
        DataPathsIds,
        SelfPeers,
        ExtraOuts,
        PeersRules,
        Transition,
        ModuleGlobsData,
        QueriedPeers
    );
};

/// @brief class representing module state including vars, dirs and vital properties
class TModule : private TNonCopyable {
public:
    // SrcDirs where sources searches. SrcDirs[0] == Dir.
    TDirs SrcDirs, Peers;
    TModuleIncDirs IncDirs;
    THolder<TDirs> MissingDirs;     // Missing addincls and srcdir for proper work from cache
    THolder<TDirs> DataPaths;
    TSet<TString> ExternalResources;
    TVars Vars;
    TPeersRestrictions PeersRestrictions;
    TVector<TString> Provides;
    THashMap<TFileElemId, EGhostType> GhostPeers; // dir ElemId -> material/virtual
    THashSet<TResolveResult> ResolveResults;
    TRawIncludes RawIncludes;
    TVector<ui32> SelfPeers;
    TVector<TFileElemId> ExtraOuts;
    ETransition Transition;
    TModuleGlobsData ModuleGlobsData;
    THashSet<TFileElemId> QueriedPeers;

    explicit TModule() = delete; // Must initialize IncDirs.

    void Save(TModuleSavedState& saved) const;

    void Init(TString fileName, TString globalFileName, TString baseName, const TModuleConf& conf);

    TFileView GetName() const;

    TStringBuf Get(const TStringBuf& name) const;
    void Set(const TStringBuf& name, const TStringBuf& value);
    bool Enabled(const TStringBuf& path) const;

    const TString& UnitName() const;
    const TString& UnitFileName() const;
    TStringBuf UnitPath() const;

    void GetModuleDirs(const TStringBuf& dirType, TVector<TString>& dirs) const;
    TVars ModuleDirsToVars() const;

    bool AddEntry(TFileElemId id);

    TOwnEntries& GetSharedEntries() const;
    TOwnEntries& GetOwnEntries() const;

    EMakeNodeType GetNodeType() const {
        Y_ASSERT(NodeType != EMNT_Deleted);
        return NodeType;
    }

    EPeerdirType GetPeerdirType() const {
        Y_ASSERT(PeerdirType != EPT_Unset);
        return PeerdirType;
    }

    bool HasId() const {
        return Id != BAD_MODULE;
    }

    TFileElemId GetId() const {
        Y_ASSERT(HasId());
        return Id;
    }

    void FinalizeConfig(TFileElemId id, const TModuleConf&);

    bool IsGlobVarsComplete() const noexcept {
        return GlobVarsComplete;
    }

    void SetGlobVarsComplete() noexcept {
        GlobVarsComplete = true;
    }

    bool IsInputsComplete() const noexcept {
        return InputsComplete;
    }

    void SetInputsComplete() noexcept;

    bool IsDirsComplete() const noexcept {
        return IncludesComplete;
    }

    /// Marks that Dirs are now trustworthy
    void SetDirsComplete() noexcept;

    void ResetIncDirs() {
        IncDirs.ResetPropagatedDirs();
    }

    bool IsPeersComplete() const {
        return PeersComplete;
    }

    bool IsDependencyManagementApplied() const noexcept {
        return Attrs.RequireDepManagement && IsPeersComplete();
    }

    /// Marks that Vars are collected
    void SetPeersComplete() noexcept;

    bool IsToolsComplete() const noexcept {
        return ToolsComplete;
    }

    void SetToolsComplete() noexcept;

    bool IsLoaded() const {
        return Loaded;
    }

    bool IsInitComplete() const {
        return InitComplete;
    }

    bool IsAccessed() const {
        return Accessed;
    }

    void SetAccessed() {
        Accessed = 1;
    }

    void NotifyInitComplete() {
        Y_ASSERT(HasId());
        InitComplete = true;
    }

    void NotifyBuildComplete() {
        BuildComplete = true;
        OnBuildCompleted();
    }

    bool IsCommitted() const {
        Y_ASSERT(!Committed || HasId());
        return Committed;
    }

    void SetCheckProvides(bool val) noexcept {
        Attrs.CheckProvides = val;
    }

    void SetFromMultimodule() {
        Attrs.FromMultimodule = true;
    }

    TFileElemId GetDirId() const {
        return DirId;
    }

    const TFileView GetDir() const {
        return Symbols.FileNameById(DirId);
    }

    // Returns module type written by user in ya.make file.
    TString GetUserType() const;

    const TStringBuf GetTag() const {
        return Tag;
    }

    const TStringBuf GetLang() const {
        // Currently GetLang is used to determine used python version in the module.
        // This information matches the tag for python2 and Go.
        if (Vars.Contains(NVariableDefs::VAR_MODULE_LANG)) {
            return Vars.EvalValue(NVariableDefs::VAR_MODULE_LANG);
        }
        return GetTag();
    }

    TFileElemId GetMakefileId() const {
        return MakefileId;
    }

    const TFileView GetMakefile() const {
        return Symbols.FileNameById(MakefileId);
    }

    const TString& GetFileName() const {
        return FileName;
    }

    TFileView GetGlobalFileName() const {
        if (!GlobalName.IsValid()) {
            auto& fileConf = Symbols.FileConf;
            GlobalName = fileConf.CreateFile(fileConf.ReplaceRoot(GetDir(), NPath::Build), GlobalFileName);
        }
        return GlobalName;
    }

    TFileElemId GetGlobalLibId() const {
        return GlobalLibId;
    }

    void SetGlobalLibId(TFileElemId id) {
        GlobalLibId = id;
    }

    void SetFileName(const TStringBuf& name) {
        Y_ASSERT(!Committed);
        FileName = name;
        auto& fileConf = Symbols.FileConf;
        Name = fileConf.CreateFile(fileConf.ReplaceRoot(GetDir(), NPath::Build), FileName);
    }

    const TModuleAttrs GetAttrs() const {
        Y_ASSERT(Committed);
        return Attrs;
    }

    EPeerSearchStatus MatchPeer(const TModule& peer, const TMatchPeerRequest& request) const;

    bool PassPeers() const {
        return Attrs.PassPeers;
    }

    bool IsCompleteTarget() const {
        return Attrs.UsePeers;
    }

    bool IsFinalTarget() const {
        return Attrs.FinalTarget;
    }

    bool IsFakeModule() const {
        return Attrs.Fake;
    }

    bool IsGoModule() const {
        return Tag.StartsWith("GO");
    }

    bool IsFromMultimodule() const {
        return Attrs.FromMultimodule;
    }

    bool NeedGoDepsCheck() const {
        return Attrs.NeedGoDepsCheck;
    }

    bool IsStartTarget() const {
        return Attrs.IsStartTarget;
    }

    void MarkFake() {
        Attrs.Fake = true;
    }

    void SetSemIgnore() {
        Attrs.SemIgnore = true;
    }

    bool IsSemIgnore() const {
        return Attrs.SemIgnore;
    }

    void SetSemForeign() {
        Attrs.SemForeign = true;
    }

    bool IsSemForeign() const {
        return Attrs.SemForeign;
    }

    bool IsExtraOut(TFileElemId elemId) const {
        return std::find(ExtraOuts.begin(), ExtraOuts.end(), elemId) != ExtraOuts.end();
    }

    bool IgnoreDupSrc() const {
        return Attrs.IgnoreDupSrc;
    }

private:
    friend class TModules;
    static constexpr const TFileElemId BAD_MODULE = TFileElemId(0xfffffffe);

    EMakeNodeType NodeType = EMNT_Deleted;
    TFileElemId Id = BAD_MODULE;

    TFileView Name;
    TString FileName; // Module output file name
    TString BaseName;
    TString GlobalFileName;
    mutable TFileView GlobalName;

    TFileElemId DirId = TFileElemId();   // Module directory ElemId
    TFileElemId MakefileId = TFileElemId();
    TFileElemId GlobalLibId = TFileElemId();

    TString Tag;         // Discriminating tag for modules belonging to the same makefile
    EPeerdirType PeerdirType = EPT_Unset;
    TAppliedPeersRules PeersRules;

    TModuleAttrs Attrs;

    union {
        ui32 AllInitializationFlags = 0;
        struct { // 10 bits used
            ui32 Loaded: 1;
            ui32 Committed: 1;
            ui32 InitComplete: 1;
            ui32 InputsComplete: 1;
            ui32 PeersComplete: 1;
            ui32 ToolsComplete: 1;
            ui32 IncludesComplete: 1;
            ui32 Accessed: 1;
            ui32 GlobVarsComplete: 1;
            ui32 BuildComplete: 1;
        };
    };

    TSymbols& Symbols;

    mutable THolder<TOwnEntries> OwnEntries;
    THolder<TOwnEntries>& SharedEntries;

    TVector<TCmdElemId> ConfigVars;

    TModule(TFileView dir, TStringBuf makefile, TStringBuf tag, TModulesSharedContext& context);
    TModule(TModuleSavedState&& saved, TModulesSharedContext& context);

    void SetupPeerdirRestrictions();
    void ImportPeerdirTags();
    void ImportPeerdirRules();
    void ImportPeerdirPolicy();
    void AddInternalRule();

    void TrimVars();
    void OnBuildCompleted();
    void ComputeConfigVars();

    bool IsStaticLib() const {
        return GetNodeType() == EMNT_Library && !IsCompleteTarget();
    }

    ~TModule() = default;
};

void DumpModuleInfo(IOutputStream& out, const TModule& module);
void DumpModuleInfoJson(NJsonWriter::TBuf& json, const TModule& module);
