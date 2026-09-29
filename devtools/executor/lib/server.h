#pragma once

#include <util/generic/string.h>

void RunServer(TString address, bool cacheStderr = true, bool debug = false, bool privateNetNs = false);
