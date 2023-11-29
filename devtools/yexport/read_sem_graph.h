#pragma once

#include "sem_graph.h"

#include <util/generic/vector.h>

#include <filesystem>

class TSemGraph;

class TReadGraphException: public yexception {};

std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(const std::filesystem::path& path, bool useManagedPeersClosure = false);
std::pair<THolder<TSemGraph>, TVector<TNodeId>> ReadSemGraph(IInputStream& in, bool useManagedPeersClosure = false);
