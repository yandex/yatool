#pragma once

#include "export_file_manager.h"
#include "options.h"

#include <devtools/yexport/diag/exception.h>
#include <devtools/yexport/diag/trace.h>

#include <util/generic/ptr.h>
#include <util/generic/vector.h>

#include <filesystem>

namespace NYexport {

enum class ECleanIgnored {
    Enabled,
    Disabled
};

class TYexportGenerator {
public:
    TYexportGenerator() noexcept = default;
    virtual ~TYexportGenerator() = default;

    virtual void LoadSemGraph(const std::string& platform, const fs::path& semGraph) = 0;
    virtual void SetProjectName(const std::string& projectName) = 0;

    void RenderTo(const fs::path& exportRoot, const fs::path& projectRoot = {}, ECleanIgnored cleanIgnored = ECleanIgnored::Disabled);
    TExportFileManager* GetExportFileManager();

    virtual void DumpSems(IOutputStream& out) const = 0; ///< Get dump of semantics tree with values for testing or debug
    virtual void DumpAttrs(IOutputStream& out) = 0; ///< Get dump of attributes tree with values for testing or debug
    virtual bool IgnorePlatforms() const = 0;///< Generator ignore platforms and wait strong one sem-graph as input

protected:
    virtual void Render(ECleanIgnored cleanIgnored) = 0;

    TVector<std::pair<fs::path, fs::path>> Copies_;///< Collect copies before ExportFileManager created
    THolder<TExportFileManager> ExportFileManager_;
};

THolder<TYexportGenerator> Load(const TOpts& opts);
TVector<std::string> GetAvailableGenerators(const fs::path& arcadiaRoot);

}
