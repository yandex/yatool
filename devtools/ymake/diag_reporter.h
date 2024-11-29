#pragma once

#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_store.h>

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/diag/manager.h>

class TModule;
class TModules;

struct TForeignPlatformEventsReporterEntryStats: public TEntryStats {
    bool ToolReported = false;

    explicit TForeignPlatformEventsReporterEntryStats(TItemDebug itemDebug = {}, bool inStack = false, bool isFile = false)
        : TEntryStats(itemDebug, inStack, isFile)
    {
    }
};

class TForeignPlatformEventsReporter: public TNoReentryStatsConstVisitor<TForeignPlatformEventsReporterEntryStats> {
public:
    using TBase = TNoReentryStatsConstVisitor<TForeignPlatformEventsReporterEntryStats>;
    using TState = typename TBase::TState;

    const TSymbols& Names;
    const TModules& Modules;
    const bool RenderSemantics;

public:
    TForeignPlatformEventsReporter(const TSymbols& names, const TModules& modules, bool renderSemantics)
        : Names(names)
        , Modules(modules)
        , RenderSemantics(renderSemantics)
    {}

    bool Enter(TState& state);
    bool AcceptDep(TState& state);
};

struct TConfigureEventsReporterEntryStats: public TEntryStats {
    bool WasFresh = false;

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

class TDupSrcReporter: public TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats> {
public:
    using TBase = TNoReentryStatsConstVisitor<TConfigureEventsReporterEntryStats>;
    using TState = typename TBase::TState;

    const TModules& Modules;
    const bool RenderSemantics;

public:
    TDupSrcReporter(const TModules& modules, bool renderSemantics)
        : Modules(modules)
        , RenderSemantics(renderSemantics)
    {}

    bool Enter(TState& state);
    bool AcceptDep(TState& state);
    void Leave(TState& state);
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

class TErrorsGuard {
public:
    TErrorsGuard(const TBuildConfiguration& conf)
    : HasErrorsFilePath_(conf.BuildRoot / "has_errors")
    {
        HasErrorsOnPrevLaunch_ = HasErrorsFilePath_.Exists();
        TFile(HasErrorsFilePath_, CreateAlways | WrOnly);
    }

    ~TErrorsGuard() {
        if (!Diag()->HasConfigurationErrors) {
            HasErrorsFilePath_.DeleteIfExists();
        }
    }

    bool HasConfigureErrorsOnPrevLaunch() const {
        return HasErrorsOnPrevLaunch_;
    }
private:
    TFsPath HasErrorsFilePath_;
    bool HasErrorsOnPrevLaunch_ = false;
};
