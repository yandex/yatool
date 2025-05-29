#pragma once

#include "std_helpers.h"

#include <util/generic/hash_set.h>
#include <util/system/file.h>

namespace NYexport {

/*
 * Class to track all exported files.
 * To ensure proper file tracking, it is better to give only this class a path to the export root,
 * thus, operations with all files in export root will be performed only through this class
 */
class TExportFileManager {
public:
    TExportFileManager(const fs::path& exportRoot, const fs::path& projectRoot);
    ~TExportFileManager();

    TFile Open(const fs::path& relativeToRoot);
    bool Copy(const fs::path& source, const fs::path& destRelativeToRoot, bool logError = true);
    bool CopyFromExportRoot(const fs::path& sourceRelativeToRoot, const fs::path& destRelativeToRoot, bool logError = true);
    bool Exists(const fs::path& relativeToRoot);
    void Remove(const fs::path& relativeToRoot);
    TString MD5(const fs::path& relativeToRoot);
    fs::path AbsPath(const fs::path& relativeToRoot);
    bool Save(const fs::path& relativeToRoot, const TString& content);

    //! Do not use this function to perform operations with files in ExportRoot (that may break generated file tracking)
    const fs::path& GetExportRoot() const;
    const fs::path& GetProjectRoot() const;

private:
    THashSet<fs::path> CreatedFiles_;
    fs::path ExportRoot_;
    fs::path ProjectRoot_;

    TFile AbsOpen(const fs::path& absPath);
};

}
