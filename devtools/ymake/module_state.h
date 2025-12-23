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

void InitModuleVars(TVars& vars, TVars& commandConf, ui32 makeFileId, TFileView moduleDir);

union TModuleAttrs {
    ui32 AllBits = 0;
    struct { // 23 bits used
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
    };
};

static_assert(sizeof(TModuleAttrs::AllBits) == sizeof(TModuleAttrs));

using TSharedEntriesMap = THashMap<ui32, THolder<TOwnEntries>>;

struct TModulesSharedContext {
    TSharedEntriesMap& SharedEntries;
    TSymbols& SymbolsTable;
    TVars& CommandConf;
    const TPeersRules& PeersRules;
};

struct TResolveResult {
    ui32 OrigPath;
    ui32 ResolveDir;
    ui32 ResultPath;

    static constexpr ui32 EmptyPath = std::numeric_limits<ui32>::max();

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
        return TupleHasher()(std::make_tuple(value.OrigPath, value.ResolveDir, value.ResultPath));
    }
};

using TRawIncludesInfo = THashMap<TPropertyType, TUniqVector<TDepsCacheId>>;
using TRawIncludes = THashMap<ui32, TRawIncludesInfo>;

class TModuleSavedState {
private:
    friend class TModule;

    ui32 Id = 0;
    ui32 DirId = 0;
    ui32 MakefileId = 0;
    ui32 GlobalLibId = 0;

    EMakeNodeType NodeType = EMNT_Deleted;

    EPeerdirType PeerdirType = EPT_Unset;

    TModuleAttrs Attrs;

    // BaseName=REALPRJNAME
    // FileName=PREFIX+REALPRJNAME+SUFFIX
    // Tag=
    // PeerdirTags=
    // ConfigVars ()
    TVector<ui32> ConfigVars;

    TModuleIncDirs::TSavedState IncDirs;
    TVector<ui32> SrcsDirsIds;
    TVector<ui32> MissingDirsIds;
    TVector<ui32> DataPathsIds;
    TVector<ui32> SelfPeers;
    TVector<ui32> ExtraOuts;

    TVector<ui32> OwnEntries;

    THashSet<TResolveResult> ResolveResults;

    TRawIncludes RawIncludes;

    THashMap<ui32, EGhostType> GhostPeers;

    TPeersRulesSavedState PeersRules;

    ETransition Transition;

    TModuleGlobsData ModuleGlobsData;

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
        ModuleGlobsData
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
    THashMap<ui32, EGhostType> GhostPeers; // dir ElemId -> material/virtual
    THashSet<TResolveResult> ResolveResults;
    TRawIncludes RawIncludes;
    TVector<ui32> SelfPeers;
    TVector<ui32> ExtraOuts;
    ETransition Transition;
    TModuleGlobsData ModuleGlobsData;

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

    bool AddEntry(ui32 id);

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

    ui32 GetId() const {
        Y_ASSERT(HasId());
        return Id;
    }

    void FinalizeConfig(ui32 id, const TModuleConf&);

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

    ui32 GetDirId() const {
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

    ui32 GetMakefileId() const {
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

    ui32 GetGlobalLibId() const {
        return GlobalLibId;
    }

    void SetGlobalLibId(ui32 id) {
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

    bool IsExtraOut(ui32 elemId) const {
        return std::find(ExtraOuts.begin(), ExtraOuts.end(), elemId) != ExtraOuts.end();
    }

    bool IgnoreDupSrc() const {
        return Attrs.IgnoreDupSrc;
    }

private:
    friend class TModules;
    static constexpr const ui32 BAD_MODULE = 0xfffffffe;

    EMakeNodeType NodeType = EMNT_Deleted;
    ui32 Id = BAD_MODULE;

    TFileView Name;
    TString FileName; // Module output file name
    TString BaseName;
    TString GlobalFileName;
    mutable TFileView GlobalName;

    ui32 DirId = 0;   // Module directory ElemId
    ui32 MakefileId = 0;
    ui32 GlobalLibId = 0;

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

    TVector<ui32> ConfigVars;

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
