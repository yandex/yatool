#pragma once

#include "yexport_generator.h"

#include <util/stream/output.h>
#include <util/stream/input.h>

#include <filesystem>
#include <functional>
#include <string>

namespace NYexport {

struct TContribCoords {
    std::string Name;
    std::string Version;
};

enum class EPyVer {
    Py2,
    Py3
};

using TCoordExtractor = std::function<TContribCoords(std::string_view graphModPath)>;

class TDefaultContribExtractor {
public:
    TDefaultContribExtractor(const std::filesystem::path& arcadiaRoot, EPyVer pyVer)
        : ArcadiaRoot_{arcadiaRoot}
        , PyVer_{pyVer}
    {
    }

    TContribCoords operator()(std::string_view graphModPath) const;

    std::filesystem::path GetYaMakePath(std::string_view graphModPath) const;
    std::filesystem::path GetYaMakePath(std::string_view graphModPath, EPyVer pyVer) const;
    std::string GetPipName(std::string_view graphModPath) const;
    std::string ExtractVersion(const std::filesystem::path& mkPath, IInputStream& mkContent) const;

private:
    std::filesystem::path ArcadiaRoot_;
    EPyVer PyVer_;
};

class TPyRequirementsGenerator : public TYexportGenerator {
public:
    TPyRequirementsGenerator(TCoordExtractor contribCoordsExtractor)
        : ExtractContribCoords_{std::move(contribCoordsExtractor)}
    {}

    static THolder<TPyRequirementsGenerator> Load(const std::filesystem::path& arcadiaRoot, EPyVer pyVer);

    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;
    void SetProjectName(const std::string& projectName) override;

    void Render(IInputStream& pyDepsDump, IOutputStream& dest) const;

    void Dump(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing
private:
    void Render(ECleanIgnored cleanIgnored) override;


    TCoordExtractor ExtractContribCoords_;
    std::filesystem::path PyDepsDumpPath_;
};

}
