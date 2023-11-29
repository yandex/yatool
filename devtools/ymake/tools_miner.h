
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/graph.h>

class TToolMiner {
public:
    TVector<ui32> MineTools(TConstDepNodeRef genFileNode);

private:
    THashMap<TNodeId, TVector<ui32>> MinedCache;
};
