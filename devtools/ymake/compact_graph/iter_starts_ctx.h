#pragma once

#include <util/generic/fwd.h>

struct TTarget;

struct TTraverseStartsContext {
    const TVector<TTarget>& StartTargets;
    const TVector<TTarget>& RecurseStartTargets;
    const THashSet<TTarget>& ModuleStartTargets;
};
