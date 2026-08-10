#pragma once

#include <devtools/ymake/options/configure_cache_policy.h>

namespace NConfReader {
    enum class ELoadStatus;
}

EConfigureCacheUnavailableReason ConfigureCacheUnavailableReason(NConfReader::ELoadStatus status) noexcept;
