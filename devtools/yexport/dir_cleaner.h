#pragma once

#include "sem_graph.h"
#include "path_hash.h"
#include "export_file_manager.h"

#include <util/generic/hash_set.h>

#include <filesystem>

namespace NYexport {

class TDirCleaner {
public:
    void CollectDirs(const TSemGraph& graph, const TVector<TNodeId>& startDirs);
    void Clean(TExportFileManager& exportFileManager) const;

private:
    THashSet<std::filesystem::path> DirsToRemove;
    THashSet<std::filesystem::path> SubdirsToKeep;
};

}
