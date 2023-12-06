#pragma once

#include "sem_graph.h"

#include <util/generic/vector.h>
#include <util/generic/hash.h>

#include <filesystem>

namespace NYexport {

namespace fs = std::filesystem;

class TCMakeGenerator;

struct TProjectConf;
struct TPlatform;
struct TGlobalProperties;

constexpr std::string_view ArcadiaScriptsRoot = "build/scripts";
constexpr std::string_view CmakeScriptsRoot = "build/scripts";

bool RenderCmake(const TVector<TNodeId>& startDirs, const TSemGraph& graph, const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator);
THashMap<fs::path, TSet<fs::path>> GetSubdirsTable(const TVector<TNodeId>& startDirs, const TSemGraph& graph, const TProjectConf& projectConf, TPlatform& platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator);

}
