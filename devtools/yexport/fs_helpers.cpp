#include "fs_helpers.h"

#include <library/cpp/resource/resource.h>

void SaveResource(std::string_view resource, fs::path destDir) {
    TFile out = OpenOutputFile(destDir/resource);
    const auto content = NResource::Find(resource);
    out.Write(content.data(), content.size());
}
