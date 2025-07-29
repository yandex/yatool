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

class TDumpDartProc: public TManagedPeerConstVisitor<> {
protected:
    using TBase = TManagedPeerConstVisitor<>;

    IOutputStream& Out;
    const TString DartPropertyName;

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
        : TBase{restoreContext}
        , Out(out)
        , DartPropertyName(dartPropertyName)
        , Commands(commands)
        , Option(opt)
    {
    }

    bool AcceptDep(TState& state);
    bool Enter(TState& state);

private:
    TString SubstModuleVars(const TStringBuf& data, const TModule& module) const;

private:
    const TCommands& Commands;
    EOption Option = EOption::Regular;
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
    const THashSet<ui32>& RecurseDirs;
    const THashSet<TTarget>& ModuleStartTargets;

    explicit TDumpMakeFileDartVisitor(IOutputStream& out, const THashSet<ui32>& recursesDirs, const THashSet<TTarget>& moduleStartTargets)
        : Out(out)
        , RecurseDirs(recursesDirs)
        , ModuleStartTargets(moduleStartTargets)
    {
    }

    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
};

class TDumpDartProcStartTargets: public TDumpDartProc {
private:
    using TBase = TDumpDartProc;

public:
    template <typename... Args>
    TDumpDartProcStartTargets(Args&&... args)
        : TDumpDartProc(std::forward<Args>(args)...)
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
TString DumpNodeFlags(ui32 elemId, EMakeNodeType nodeType, const TSymbols& names);
