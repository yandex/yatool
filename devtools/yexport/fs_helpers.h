#pragma once

#include <util/generic/string.h>
#include <util/system/file.h>

#include <filesystem>

namespace fs = std::filesystem;

inline TFile OpenOutputFile(const fs::path& path) {
    fs::create_directories(path.parent_path());
    return TFile{path.string(), CreateAlways};
}

void SaveResource(std::string_view resource, fs::path destDir);
