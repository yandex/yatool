#pragma once

#include "sem_graph.h"
#include "path_hash.h"

#include <util/generic/hash_set.h>

#include <filesystem>

class TDirCleaner {
public:
    void CollectDirs(const TSemGraph& graph, const TVector<TNodeId>& startDirs);
    void Clean(const std::filesystem::path& exportRoot) const;

private:
    THashSet<std::filesystem::path> DirsToRemove;
    THashSet<std::filesystem::path> SubdirsToKeep;
};
