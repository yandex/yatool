#include "export_file_manager.h"

#include <devtools/yexport/diag/trace.h>

#include <library/cpp/digest/md5/md5.h>

#include <spdlog/spdlog.h>

TExportFileManager::TExportFileManager(const fs::path& exportRoot)
    : ExportRoot_(exportRoot)
{
}
TExportFileManager::~TExportFileManager() {
    for (const auto& file : CreatedFiles_) {
        TraceFileExported(ExportRoot_ / file);
    }
}

TFile TExportFileManager::Open(const fs::path& relativeToRoot) {
    auto absPath = ExportRoot_ / relativeToRoot;
    fs::create_directories(absPath.parent_path());
    CreatedFiles_.insert(relativeToRoot);

    return TFile{absPath.string(), CreateAlways};
}
bool TExportFileManager::Copy(const fs::path& source, const fs::path& destRelativeToRoot, bool logError) {
    if (!fs::exists(source)) {
        if (logError) {
            spdlog::error("Failed to copy {} to {} because source does not exist", source.c_str(), (ExportRoot_ / destRelativeToRoot).c_str());
        }
        return false;
    }
    auto absPath = ExportRoot_ / destRelativeToRoot;
    if (fs::exists(absPath) && fs::equivalent(absPath, source)) {
        return true;
    }
    fs::create_directories(absPath.parent_path());
    fs::copy_file(source, absPath, fs::copy_options::overwrite_existing);
    CreatedFiles_.insert(destRelativeToRoot);
    return true;
}
bool TExportFileManager::CopyFromExportRoot(const fs::path& sourceRelativeToRoot, const fs::path& destRelativeToRoot, bool logError) {
    return Copy(ExportRoot_ / sourceRelativeToRoot, destRelativeToRoot, logError);
}
bool TExportFileManager::Exists(const fs::path& relativeToRoot) {
    return fs::exists(ExportRoot_ / relativeToRoot);
}
void TExportFileManager::Remove(const fs::path& relativeToRoot) {
    CreatedFiles_.erase(relativeToRoot);
    fs::remove(ExportRoot_ / relativeToRoot);
}
TString TExportFileManager::MD5(const fs::path& relativeToRoot) {
    if (!CreatedFiles_.contains(relativeToRoot)) {
        return {};
    }
    return MD5::File((ExportRoot_ / relativeToRoot).c_str());
}

fs::path TExportFileManager::GetExportRoot() const {
    return ExportRoot_;
}
