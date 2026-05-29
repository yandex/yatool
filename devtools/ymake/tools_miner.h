
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/graph.h>

class TToolMiner {
public:
    TVector<TFileElemId> MineTools(TConstDepNodeRef genFileNode);

private:
    THashMap<TNodeId, TVector<TFileElemId>> MinedCache;
};
