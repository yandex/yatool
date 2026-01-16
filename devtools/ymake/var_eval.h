#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

class TCommands;
class TCmdConf;
class TBuildConfiguration;

struct TYVar;
struct TVars;

TVector<TString> EvalAll(const TYVar& var, const TVars& vars, const TCommands& commands, const TCmdConf& cmdConf, const TBuildConfiguration& conf);
