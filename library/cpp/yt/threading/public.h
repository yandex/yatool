#pragma once

#include <cstddef>

namespace NYT::NThreading {

////////////////////////////////////////////////////////////////////////////////

#define YT_DECLARE_SPIN_LOCK(type, name) \
    type name{__LOCATION__}

////////////////////////////////////////////////////////////////////////////////

using TThreadId = size_t;
constexpr size_t InvalidThreadId = 0;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NThreading