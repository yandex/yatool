#pragma once

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/query.h>

class TDependencyFilter {
private:
    int SkipFlags;

public:
    enum ESkipType {
        SkipRecurses = 1 << 0,
        SkipModules = 1 << 1,
        SkipTools = 1 << 2,
        SkipDepends = 1 << 3,
        SkipAddincls = 1 << 4,
    };

    explicit TDependencyFilter(int skipFlags) noexcept
        : SkipFlags(skipFlags)
    {
    }
    template <class TDep>
    bool operator()(const TDep& dep, bool startNode = false) {
        if ((SkipFlags & SkipRecurses) && (IsRecurseDep(dep) || IsPropToDirSearchDep(dep))) {
            return false;
        }
        if ((SkipFlags & SkipModules) && !startNode && IsDirToModuleDep(dep)) {
            return false;
        }
        if ((SkipFlags & SkipTools) && (IsTooldirDep(dep) || IsDirectToolDep(dep))) {
            return false;
        }
        if ((SkipFlags & SkipDepends) && (IsDependsDep(dep) || IsPropToDirSearchDep(dep))) {
            return false;
        }
        if ((SkipFlags & SkipAddincls) && IsSearchDirDep(dep)) {
            return false;
        }

        return true;
    }
};

template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
class TFilteredNoReentryStatsVisitor: public TNoReentryStatsVisitor<TVisitorState, TIterStateItem, TIterState> {
private:
    TDependencyFilter Filter;

public:
    using TBase = TNoReentryStatsVisitor<TVisitorState, TIterStateItem, TIterState>;
    using typename TBase::TState;

    TFilteredNoReentryStatsVisitor(TDependencyFilter Filter)
        : Filter{Filter} {
    }

    bool AcceptDep(TState& state) {
        return Filter(state.NextDep(), !state.HasIncomingDep()) && TBase::AcceptDep(state);
    }
};

template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
class TDirectPeerdirsVisitor: public TFilteredNoReentryStatsVisitor<TVisitorState, TIterStateItem, TIterState> {
public:
    using TBase = TFilteredNoReentryStatsVisitor<TVisitorState, TIterStateItem, TIterState>;
    using typename TBase::TState;

    TDirectPeerdirsVisitor(TDependencyFilter Filter = TDependencyFilter{TDependencyFilter::SkipRecurses | TDependencyFilter::SkipAddincls})
        : TBase{Filter} {
    }

    bool AcceptDep(TState& state) {
        const auto dep = state.NextDep();
        if (IsPeerdirDep(dep)) {
            return false;
        }
        return TBase::AcceptDep(state);
    }
};

template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<true>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
using TDirectPeerdirsConstVisitor = TDirectPeerdirsVisitor<TVisitorState, TIterStateItem, TIterState>;
