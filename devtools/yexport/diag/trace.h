#pragma once

#include <filesystem>

namespace fs = std::filesystem;

void TraceFileExported(const fs::path& path);
