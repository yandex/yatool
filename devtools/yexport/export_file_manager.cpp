#include "export_file_manager.h"

#include <devtools/yexport/diag/trace.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/resource/resource.h>

#include <spdlog/spdlog.h>

namespace NYexport {

TExportFileManager::TExportFileManager(const fs::path& exportRoot, const fs::path& projectRoot)
    : ExportRoot_(exportRoot)
    , ProjectRoot_(projectRoot)
{}

TExportFileManager::~TExportFileManager() {
    for (const auto& file : CreatedFiles_) {
        TraceFileExported(AbsPath(file));
    }
}

TFile TExportFileManager::Open(const fs::path& relativeToRoot) {
    CreatedFiles_.insert(relativeToRoot);
    return AbsOpen(AbsPath(relativeToRoot));
}

TFile TExportFileManager::AbsOpen(const fs::path& absPath) {
    spdlog::debug("[TExportFileManager] Opened file: {}", absPath.c_str());
    fs::create_directories(absPath.parent_path());
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
    auto absPath = AbsPath(destRelativeToRoot);
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
    return Copy(AbsPath(sourceRelativeToRoot), destRelativeToRoot, logError);
}

bool TExportFileManager::Exists(const fs::path& relativeToRoot) {
    return fs::exists(AbsPath(relativeToRoot));
}

void TExportFileManager::Remove(const fs::path& relativeToRoot) {
    CreatedFiles_.erase(relativeToRoot);
    const auto path = AbsPath(relativeToRoot);
    NYexport::TracePathRemoved(path);
    fs::remove(path);
}

TString TExportFileManager::MD5(const fs::path& relativeToRoot) {
    if (!CreatedFiles_.contains(relativeToRoot)) {
        return {};
    }
    return MD5::File(AbsPath(relativeToRoot).string());
}

fs::path TExportFileManager::AbsPath(const fs::path& relativeToRoot) {
    return ExportRoot_ / relativeToRoot;
}

bool TExportFileManager::Save(const fs::path& relativeToRoot, const TString& content) {
    CreatedFiles_.insert(relativeToRoot);
    const auto absPath = AbsPath(relativeToRoot);
    if (fs::exists(absPath)) {
        if (MD5::File(absPath.string()) == MD5::Calc(content)) {
            return false; // already exists file with same content
        }
    }
    auto out = AbsOpen(absPath);
    out.Write(content.data(), content.size());
    return true; // created
}

const fs::path& TExportFileManager::GetExportRoot() const {
    return ExportRoot_;
}

const fs::path& TExportFileManager::GetProjectRoot() const {
    return ProjectRoot_.empty() ? ExportRoot_ : ProjectRoot_;
}

}
