#pragma once

#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_store.h>

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/diag/manager.h>

class TModule;
class TModules;

struct TConfigureEventsReporterEntryStats: public TEntryStats {
    bool WasFresh = false;
    const TModule* PrevModule = nullptr;
    bool ToolReported = false;

    explicit TConfigureEventsReporterEntryStats(TItemDebug itemDebug = {}, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
    {
    }
};

class TConfigureEventsReporter: public TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats> {
public:
    using TBase = TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats>;
    using TState = typename TBase::TState;

    const TSymbols& Names;
    const TModules& Modules;
    const TModule* CurModule = nullptr;
    const bool RenderSemantics;

public:
    TConfigureEventsReporter(const TSymbols& names, const TModules& modules, bool renderSemantics)
        : Names(names)
        , Modules(modules)
        , RenderSemantics(renderSemantics)
    {
        ConfMsgManager()->DisableDelay();
    }

    bool Enter(TState& state);
    bool AcceptDep(TState& state);
    void Leave(TState& state);

private:
    void PushModule(TConstDepNodeRef modNode);
    void PopModule();
};

class TRecurseConfigureErrorReporter: public TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats> {
public:
    using TBase = TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats>;
    using TState = typename TBase::TState;

    const TSymbols& Names;

public:
    TRecurseConfigureErrorReporter(const TSymbols& names)
        : Names(names)
    {}

    bool Enter(TState& state);
    bool AcceptDep(TState& state);
};
