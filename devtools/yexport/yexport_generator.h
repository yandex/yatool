#pragma once

#include <util/generic/ptr.h>
#include <util/generic/vector.h>

#include <filesystem>

namespace fs = std::filesystem;

enum class ECleanIgnored {
    Enabled,
    Disabled
};

class TYexportGenerator {
public:
    TYexportGenerator() noexcept = default;
    virtual ~TYexportGenerator() = default;;

    virtual void LoadSemGraph(const std::string& platform, const fs::path& semGraph) = 0;
    virtual void Render(const fs::path& exportRoot, ECleanIgnored cleanIgnored = ECleanIgnored::Disabled) = 0;
    virtual void SetProjectName(const std::string& projectName) = 0;
};

THolder<TYexportGenerator> Load(const std::string& generator, const fs::path& arcadiaRoot, const fs::path& configDir = "");
TVector<std::string> GetAvailableGenerators(const fs::path& arcadiaRoot);
