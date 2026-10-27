#pragma once

#include "conf.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/loops.h>
#include <devtools/ymake/managed_deps_iter.h>

#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/utility.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

#include <utility>

class IOutputStream;
class TModules;
struct TRestoreContext;
class TCommands;
struct TExecContext;

struct TDepthData {
    size_t Depth;

    TDepthData()
        : Depth(0)
    {
    }
    void CheckDepth(size_t childDepth) {
        Depth = Max(Depth, childDepth + 1);
    }
};

using TDepthDumpStateItem = TGraphIteratorStateItem<TDepthData, true>;

struct TDumpEntrySt: public TEntryStats {
    size_t Depth;
    union {
        ui32 AllFreshFlags = 0;
        struct { // 3 bits used
            ui32 RejectedByIncomingDep : 1;
            ui32 WasPrintedAsMakeFile : 1;
            ui32 WasPrintedAsFile : 1;
        };
    };

    TDumpEntrySt(TItemDebug itemDebug = {}, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
        , Depth(0)
    {
    }
};

class TDepDirsProc: public TNoReentryStatsConstVisitor<> {
private:
    using TBase = TNoReentryStatsConstVisitor<>;

    enum EDepDirType {
        DDT_Recurse,
        DDT_Peerdir,
    };

    enum EDumpMode {
        DM_RecurseOnly,
        DM_PeerdirsOnly,
        DM_PeerdirsAndRecurses,
    };
    EDumpMode Mode;
    THashMap<TNodeId, TVector<std::pair<TNodeId, EDepDirType>>> DependentDirs;

public:
    explicit TDepDirsProc(const TDebugOptions& cf)
        : Mode(cf.DumpPeers ? DM_PeerdirsOnly : cf.DumpRecurses ? DM_RecurseOnly : DM_PeerdirsAndRecurses)
    {
    }

    bool Enter(TState& state);
    bool AcceptDep(TState& state);

    const decltype(DependentDirs) & GetDependentDirs() const {
        return DependentDirs;
    }
};

class TDumpDartVisitor: public TManagedPeerConstVisitor<> {
protected:
    using TBase = TManagedPeerConstVisitor<>;

public:
    TDumpDartVisitor(const TRestoreContext& restoreContext, const TString& dartPropertyName)
        : TBase{restoreContext}
        , DartPropertyName(dartPropertyName)
    {
    }

    virtual ~TDumpDartVisitor() = default;
    bool AcceptDep(TState& state);
    bool Enter(TState& state);

protected:
    virtual void OnProperty(TConstDepNodeRef module, TStringBuf value) = 0;

private:
    const TString DartPropertyName;
};

class TDumpDartProc: public TDumpDartVisitor {
private:
    using TBase = TDumpDartVisitor;

public:
    enum EOption {
        Regular,
        Encoded,
        SubstVarsGlobaly
    };

    TDumpDartProc(
        const TRestoreContext& restoreContext,
        const TCommands& commands,
        IOutputStream& out,
        const TString& dartPropertyName,
        EOption opt = EOption::Regular
    )
        : TBase(restoreContext, dartPropertyName)
        , Out(out)
        , Commands(commands)
        , Option(opt)
    {
    }

private:
    void OnProperty(TConstDepNodeRef module, TStringBuf value) override;

    IOutputStream& Out;
    const TCommands& Commands;
    EOption Option;
};

class TPreparedDartModule {
public:
    TPreparedDartModule(TRestoreContext context, TConstDepNodeRef node);

    const TModule& GetModule() const;
    void UpdateVars(TVars& vars, const TBuildConfiguration& conf, const TDepGraph& graph) const;

private:
    const TModule* Module_;
    const TVars* Globals_;
    const TUniqVector<TNodeId>* ManagedPeers_ = nullptr;
    const TUniqVector<TNodeId>* ManagedDirectPeers_ = nullptr;
    TVector<const TVars*> PeerGlobals_;
    struct TExcludedModule {
        const TModule* Module;
        const TVars* Globals;
    };
    TVector<TExcludedModule> ExcludedModules_;
    bool HasExcludes_ = false;
};

class TTestDartRenderPlan: TNonCopyable {
public:
    explicit TTestDartRenderPlan(TYMake& ymake);
    ~TTestDartRenderPlan();

    void Render(IOutputStream& out) const;
    void Render(IOutputStream& out, TExecContext& context) const;

private:
    struct TImpl;
    THolder<TImpl> Impl_;
};

struct TMakeFileData {
    TMakeFileData()
        : OwnersNode()
        , Fresh(false)
    {
    }

    TDepTreeNode OwnersNode;
    bool Fresh;
};

using TDumpMakeFileDartStateItem = TGraphIteratorStateItem<TMakeFileData, true>;

class TDumpMakeFileDartVisitor: public TNoReentryStatsVisitor<TEntryStats, TDumpMakeFileDartStateItem> {
private:
    const TStringBuf DELIMITER = "==========";
    using TBase = TNoReentryStatsVisitor<TEntryStats, TDumpMakeFileDartStateItem>;

public:
    IOutputStream& Out;
    const THashSet<TFileElemId>& RecurseDirs;
    const THashSet<TTarget>& ModuleStartTargets;

    explicit TDumpMakeFileDartVisitor(IOutputStream& out, const THashSet<TFileElemId>& recursesDirs, const THashSet<TTarget>& moduleStartTargets)
        : Out(out)
        , RecurseDirs(recursesDirs)
        , ModuleStartTargets(moduleStartTargets)
    {
    }

    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
};

class TDumpDartProcStartTargets: public TDumpDartVisitor {
private:
    using TBase = TDumpDartVisitor;

public:
    template <typename... Args>
    TDumpDartProcStartTargets(Args&&... args)
        : TBase(std::forward<Args>(args)...)
    {
    }

    bool AcceptDep(TState& state);
};

struct TBuildTargetDepsEntStats : TEntryStats {
    THashSet<TNodeId> InclFiles;
    THashSet<TNodeId> BFFiles;

    bool WasFresh = false;
    bool IsModule = false;
    TNodeId LoopId;

    TBuildTargetDepsEntStats(TItemDebug itemDebug, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
        , LoopId(TNodeId::Invalid)
    {
    }
};

class TBuildTargetDepsPrinter: public TNoReentryStatsConstVisitor<TBuildTargetDepsEntStats> {
private:
    using TBase = TNoReentryStatsConstVisitor<TBuildTargetDepsEntStats>;

    TGraphLoops& Loops;
    IOutputStream& Cmsg;

public:
    TBuildTargetDepsPrinter(TGraphLoops& loops, IOutputStream& cmsg)
        : Loops(loops)
        , Cmsg(cmsg)
    {
    }

    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);

    bool AcceptDep(TState& state);
};

void DumpModulesInfo(IOutputStream& out, const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TString& filter);
TString DumpNodeFlags(TElemId elemId, EMakeNodeType nodeType, const TSymbols& names);

class TDartManager {
public:
    enum class EDartType {
        None,
        Test,
        Java,
        Makefiles
    };

    TDartManager(TYMake& ymake)
        : YMake_(ymake)
    {
    }

    ~TDartManager() = default;

    void DumpTestDart();
    void Dump(EDartType dartType, const TString& dartName);
private:
    bool TryLoad(EDartType dartType, const TFsPath& dartPath, const TFsPath& cachePath);
    void Render(EDartType dartType, const TFsPath& dartPath);
    void Save(EDartType dartType, const TFsPath& dartPath, const TFsPath& cachePath);

    TYMake& YMake_;
};
