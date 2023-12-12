#pragma once

#include "path_hash.h"

#include <util/generic/hash_set.h>
#include <util/system/file.h>
#include <filesystem>

namespace NYexport {

namespace fs = std::filesystem;

/*
 * Class to track all exported files.
 * To ensure proper file tracking, it is better to give only this class a path to the export root,
 * thus, operations with all files in export root will be performed only through this class
 */
class TExportFileManager {
public:
    TExportFileManager(const fs::path& exportRoot);
    ~TExportFileManager();

    TFile Open(const fs::path& relativeToRoot);
    bool Copy(const fs::path& source, const fs::path& destRelativeToRoot, bool logError = true);
    bool CopyFromExportRoot(const fs::path& sourceRelativeToRoot, const fs::path& destRelativeToRoot, bool logError = true);
    bool CopyResource(const fs::path& relativeToRoot, bool logError = true);
    bool Exists(const fs::path& relativeToRoot);
    void Remove(const fs::path& relativeToRoot);
    TString MD5(const fs::path& relativeToRoot);

    //! Do not use this function to perform operations with files in ExportRoot (that may break generated file tracking)
    fs::path GetExportRoot() const;

private:
    THashSet<fs::path> CreatedFiles_;
    fs::path ExportRoot_;
};

}
