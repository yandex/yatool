#pragma once

#include "options.h"

namespace NYexport {

void SetupLogger(TLoggingOpts opts);
bool IsFailOnError();

}
