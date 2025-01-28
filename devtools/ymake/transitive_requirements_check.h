#pragma once

#include "spdx.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/transitive_constraints.h>

#include <util/generic/vector.h>

struct TRestoreContext;
class TBuildConfiguration;
struct TVars;

void CheckTransitiveRequirements(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets);

struct TTransitiveCheckRegistryItem {
    using TRequirementsLoader = std::function<TTransitiveRequirement(TDepGraph&, const TBuildConfiguration&, const TModule&)>;
    using TRequirementLoaderFactory = TRequirementsLoader(const TVars&);

    TArrayRef<const TStringBuf> ConfVars;
    TRequirementLoaderFactory* RequirementLoaderFactory = nullptr;
};
extern const TArrayRef<const TTransitiveCheckRegistryItem> TRANSITIVE_CHECK_REGISTRY;

// TODO(svidyuk) Some generic code to add arbitrary queries related to transitive checks?
void DoDumpLicenseInfo(const TBuildConfiguration& conf, const TVars& globals, NSPDX::EPeerType peerType, bool humanReadable, TArrayRef<TString> tagVars);
