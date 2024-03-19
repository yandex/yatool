#pragma once

#include "sem_graph.h"
#include "std_helpers.h"

#include <util/generic/vector.h>
#include <util/generic/hash.h>

#include <filesystem>

namespace NYexport {

class TCMakeGenerator;

struct TProjectConf;
struct TPlatform;
struct TGlobalProperties;

constexpr std::string_view ArcadiaScriptsRoot = "build/scripts";
constexpr std::string_view CmakeScriptsRoot = "build/scripts";

bool RenderCmake(const TProjectConf& projectConf, const TSimpleSharedPtr<TPlatform> platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator);
THashMap<fs::path, TSet<fs::path>> GetSubdirsTable(const TProjectConf& projectConf, const TSimpleSharedPtr<TPlatform> platform, TGlobalProperties& globalProperties, TCMakeGenerator* cmakeGenerator);

}
