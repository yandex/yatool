#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <functional>
#include <vector>

namespace NYMake {
    // Keeps the task closures alive and waits for every task before propagating
    // the first error in completion order. Cancellation also joins all tasks.
    // Cancellation is cooperative: a task that blocks indefinitely prevents
    // completion. Returning early would invalidate state borrowed by the tasks.
    asio::awaitable<void> RunAll(asio::any_io_executor executor,
                               std::vector<std::function<asio::awaitable<void>()>> tasks);
}
