#pragma once

#include "sem_graph.h"
#include "std_helpers.h"

#include <util/generic/vector.h>

namespace NYexport {

class TSemGraph;

class TReadGraphException: public yexception {};

std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(const fs::path& path, bool useManagedPeersClosure = false);
std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(IInputStream& in, bool useManagedPeersClosure = false);

}
