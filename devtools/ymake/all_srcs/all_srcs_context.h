#pragma once

#include <devtools/ymake/compact_graph/dep_types.h>
#include <util/generic/vector.h>

class TModuleBuilder;
class TDepTreeNode;
class TDepGraph;
struct TVars;
struct TNodeAddCtx;


class TAllSrcsContext {
    friend class TModuleBuilder;
public:
    static constexpr auto NodeType = EMNT_BuildCommand;
    static constexpr auto DepType  = EDT_Property;

    void AddDep(TDepTreeNode depNode);
    bool IsAllSrcsNode(const TNodeAddCtx* other);

private:
    void InitializeNode(TModuleBuilder& builder);

    TVector<TDepTreeNode> TemporalDepStorage;
    TNodeAddCtx* Node = nullptr;
};
