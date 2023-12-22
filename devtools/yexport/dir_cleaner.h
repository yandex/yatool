#pragma once

#include "sem_graph.h"
#include "std_helpers.h"
#include "export_file_manager.h"

#include <util/generic/hash_set.h>

#include <filesystem>

namespace NYexport {

class TDirCleaner {
public:
    void CollectDirs(const TSemGraph& graph, const TVector<TNodeId>& startDirs);
    void Clean(TExportFileManager& exportFileManager) const;

private:
    THashSet<fs::path> DirsToRemove;
    THashSet<fs::path> SubdirsToKeep;
};

}
