#pragma once

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/symbols/globs.h>

#include <library/cpp/regex/pcre/regexp.h>
#include <util/generic/array_ref.h>
#include <util/generic/algorithm.h>
#include <util/generic/flags.h>

class TDepGraph;
class TSymbols;
class TModule;
struct TRestoreContext;

enum class ERequirementsScope : ui32 {
    Peers = 0b0001,
    Tools = 0b0010,
};
using TRequrementScopes = TFlags<ERequirementsScope>;
constexpr auto REQUIREMENT_SCOPE_ALL = TRequrementScopes{} | ERequirementsScope::Peers | ERequirementsScope::Tools;

struct TScopeClosureRef {
    TArrayRef<const TNodeId> Closure;
    ERequirementsScope Scope;
};

struct TTransitiveRequirement {
    std::function<void(TRestoreContext, const TModule&, TNodeId, TScopeClosureRef)> Check;
    TRequrementScopes Scopes;
};

enum class EConstraintsType {
    Deny,
    AllowOnly
};

template <typename TPathConstraint>
class TDependencyConstraint {
private:
    TRequrementScopes Scope;

public:
    TRequrementScopes GetScope() const noexcept {
        return Scope;
    }

    bool Matches(TFileView path, ERequirementsScope scope) const noexcept(noexcept(std::declval<TPathConstraint>().CheckPath(path))) {
        return (Scope & scope) && static_cast<const TPathConstraint&>(*this).CheckPath(path);
    }

protected:
    TDependencyConstraint(TRequrementScopes scope = REQUIREMENT_SCOPE_ALL) noexcept
        : Scope{scope} {
    }
};

class TPrefixConstraint: public TDependencyConstraint<TPrefixConstraint> {
public:
    TPrefixConstraint(TDepGraph& graph, TStringBuf peerPrefix, TRequrementScopes scope);
    TPrefixConstraint(TSymbols& symbols, ui32 dirId, TRequrementScopes scope);

    TFileView GetPeerPrefix() const noexcept {
        return PeerPrefix;
    }

    bool CheckPath(TFileView path) const noexcept {
        return NPath::IsPrefixOf(PeerPrefix.GetTargetStr(), path.GetTargetStr());
    };

private:
    TFileView PeerPrefix;
};

class TGlobConstraint: public TDependencyConstraint<TGlobConstraint> {
public:
    TGlobConstraint(TStringBuf glob, TRequrementScopes scope);

    bool CheckPath(TFileView path) const {
        return MatchPath(Glob, path);
    };

private:
    TRegExMatch Glob;
};

class TDependencyConstraints {
public:
    explicit TDependencyConstraints(EConstraintsType type) noexcept
        : Type{type} {
    }

    bool IsAllowed(TFileView path, ERequirementsScope scope) const;

    bool HasRestrictions(ERequirementsScope scope) const noexcept {
        auto matchesScope = [scope](const auto& constraint) {
            return static_cast<bool>(constraint.GetScope() & scope);
        };
        return AnyOf(Prefixes, matchesScope) || AnyOf(Globs, matchesScope);
    }

    bool Empty() const noexcept {
        return Prefixes.empty() && Globs.empty();
    }

    void ValidatePaths(TDepGraph& graph) const;

    void Add(TPrefixConstraint constraint) {
        Prefixes.push_back(constraint);
    }

    void Add(TGlobConstraint constraint) {
        Globs.push_back(constraint);
    }

private:
    EConstraintsType Type;
    TVector<TPrefixConstraint> Prefixes;
    TVector<TGlobConstraint> Globs;
};
