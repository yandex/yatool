#pragma once

#include <filesystem>

namespace NYexport {

namespace fs = std::filesystem;

void TraceFileExported(const fs::path& path);
void TracePathRemoved(const fs::path& path);
void TraceStageStat(const std::string& stage, double sumSec, int calls, double minSec, double avrSec, double maxSec);

}
