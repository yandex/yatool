#pragma once

#include "build_result.h"

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <util/generic/maybe.h>

#include <functional>

// Both callbacks and their borrowed data stay alive until all analyses finish,
// including early build results, exceptions and cooperative cancellation.
asio::awaitable<TMaybe<EBuildResult>> RunConcurrentAnalyses(
    asio::any_io_executor executor,
    std::function<void()> checkIsolatedProjects,
    std::function<TMaybe<EBuildResult>()> otherAnalyses);
