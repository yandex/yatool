#include "export_file_manager.h"

#include <devtools/yexport/diag/trace.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/resource/resource.h>

#include <spdlog/spdlog.h>

namespace NYexport {

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
    spdlog::debug("[TExportFileManager] Opened file: {}", relativeToRoot.c_str());
    return TFile{absPath, CreateAlways};
}
bool TExportFileManager::Copy(const fs::path& source, const fs::path& destRelativeToRoot, bool logError) {
    if (!fs::exists(source)) {
        if (logError) {
            spdlog::error("[TExportFileManager] Failed to copy file: {}", source.c_str());
        } else {
            spdlog::debug("[TExportFileManager] Failed to copy file: {}", source.c_str());
        }
        return false;
    }
    auto absPath = ExportRoot_ / destRelativeToRoot;
    if (fs::exists(absPath) && fs::equivalent(absPath, source)) {
        spdlog::debug("[TExportFileManager] Skipping file copy becase source is same as destination: {}", source.c_str());
        return true;
    }
    fs::create_directories(absPath.parent_path());
    fs::copy_file(source, absPath, fs::copy_options::overwrite_existing);
    CreatedFiles_.insert(destRelativeToRoot);
    spdlog::debug("[TExportFileManager] Copied file: {}", source.c_str());
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
    const auto path = ExportRoot_ / relativeToRoot;
    NYexport::TracePathRemoved(path);
    fs::remove(path);
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

}
