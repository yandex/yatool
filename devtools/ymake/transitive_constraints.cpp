#include "transitive_constraints.h"
#include "module_restorer.h"
#include "module_state.h"
#include "module_store.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/symbols/globs.h>

namespace {
    TFileView GetStoredName(TDepGraph& graph, TStringBuf peerPrefix) {
        return graph.Names().FileConf.GetStoredName(peerPrefix);
    }
}

TPrefixConstraint::TPrefixConstraint(TDepGraph& graph, TStringBuf peerPrefix, TRequrementScopes scope)
    : TDependencyConstraint<TPrefixConstraint>{scope}
    , PeerPrefix{GetStoredName(graph, peerPrefix)} {
}

TPrefixConstraint::TPrefixConstraint(TSymbols& symbols, ui32 dirId, TRequrementScopes scope)
    : TDependencyConstraint<TPrefixConstraint>{scope}
    , PeerPrefix{symbols.FileNameById(dirId)} {
}

TGlobConstraint::TGlobConstraint(TStringBuf glob, TRequrementScopes scope)
    : TDependencyConstraint{scope}
    , Glob{PatternToRegexp(glob)} {
}

bool TDependencyConstraints::IsAllowed(TFileView path, ERequirementsScope scope) const {
    auto matches = [&](const auto& constraint) { return constraint.Matches(path, scope); };
    switch (Type) {
        case EConstraintsType::AllowOnly: {
            bool hasMismatchedContraintForScope = false;

            for (const auto& constraint : Prefixes) {
                if (matches(constraint)) {
                    return true;
                } else if (scope & constraint.GetScope()) {
                    hasMismatchedContraintForScope = true;
                }
            }

            for (const auto& constraint : Globs) {
                if (matches(constraint)) {
                    return true;
                } else if (scope & constraint.GetScope()) {
                    hasMismatchedContraintForScope = true;
                }
            }

            return !hasMismatchedContraintForScope;
        }
        case EConstraintsType::Deny:
            return std::none_of(Prefixes.begin(), Prefixes.end(), matches) && std::none_of(Globs.begin(), Globs.end(), matches);
    }
    return true;
}

void TDependencyConstraints::ValidatePaths(TDepGraph& graph) const {
    for (const auto& pathConstraint : Prefixes) {
        if (!graph.Names().FileConf.CheckExistentDirectory(pathConstraint.GetPeerPrefix())) {
            YConfErr(ChkDepDirExists) << "adds dependency constraint with [[alt1]]CHECK_DEPENDENT_DIRS[[rst]] to non-directory [[imp]]" << pathConstraint.GetPeerPrefix() << "[[rst]]" << Endl;
        }
    }
}
