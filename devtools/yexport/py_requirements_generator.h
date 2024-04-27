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
    TDefaultContribExtractor(const fs::path& arcadiaRoot, EPyVer pyVer)
        : ArcadiaRoot_{arcadiaRoot}
        , PyVer_{pyVer}
    {
    }

    TContribCoords operator()(std::string_view graphModPath) const;

    fs::path GetYaMakePath(std::string_view graphModPath) const;
    fs::path GetYaMakePath(std::string_view graphModPath, EPyVer pyVer) const;
    std::string GetPipName(std::string_view graphModPath) const;
    std::string ExtractVersion(const fs::path& mkPath, IInputStream& mkContent) const;

private:
    fs::path ArcadiaRoot_;
    EPyVer PyVer_;
};

class TPyRequirementsGenerator : public TYexportGenerator {
public:
    TPyRequirementsGenerator(TCoordExtractor contribCoordsExtractor)
        : ExtractContribCoords_{std::move(contribCoordsExtractor)}
    {}

    static THolder<TPyRequirementsGenerator> Load(const fs::path& arcadiaRoot, EPyVer pyVer);

    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;
    void SetProjectName(const std::string& projectName) override;

    void Render(IInputStream& pyDepsDump, IOutputStream& dest) const;

    void DumpSems(IOutputStream& out) const override; ///< Get dump of semantics tree with values for testing or debug
    void DumpAttrs(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing or debug
    bool IgnorePlatforms() const override;///< Generator ignore platforms and wait strong one sem-graph as input

private:
    void Render(ECleanIgnored cleanIgnored) override;


    TCoordExtractor ExtractContribCoords_;
    fs::path PyDepsDumpPath_;
};

}
