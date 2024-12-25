#pragma once

#include "build_result.h"
#include "conf.h"

class TBuildConfiguration;
class TYMake;

EBuildResult DumpLicenseInfo(TBuildConfiguration& conf);
EBuildResult PrintAbsTargetPath(const TBuildConfiguration& conf);

void PerformDumps(const TBuildConfiguration& conf, TYMake& yMake);
