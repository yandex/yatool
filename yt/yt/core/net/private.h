#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

namespace NYT::NNet {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, NetLogger, "Net");

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNet