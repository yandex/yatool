#include "parallel.h"

#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace NYMake {
    namespace {
        asio::awaitable<void> RunTask(std::function<asio::awaitable<void>()> task) {
            // co_spawn may dispatch inline. Queue CPU work so launching the
            // group does not execute all render chunks on the parent thread.
            co_await asio::post(asio::use_awaitable);
            co_await task();
        }

        asio::awaitable<void> RunAllOnStrand(asio::any_io_executor executor,
                                           std::vector<std::function<asio::awaitable<void>()>> tasks) {
            using TOperation = decltype(asio::co_spawn(executor, RunTask(std::move(tasks.front())), asio::deferred));
            std::vector<TOperation> operations;
            operations.reserve(tasks.size());
            for (auto& task : tasks) {
                operations.push_back(asio::co_spawn(executor, RunTask(std::move(task)), asio::deferred));
            }
            auto [order, errors] = co_await asio::experimental::make_parallel_group(std::move(operations))
                .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);
            for (auto index : order) {
                if (errors[index]) {
                    std::rethrow_exception(errors[index]);
                }
            }
        }
    }

    asio::awaitable<void> RunAll(asio::any_io_executor executor,
                               std::vector<std::function<asio::awaitable<void>()>> tasks) {
        if (tasks.empty()) {
            co_return;
        }

        // Asio launches children before registering the group's cancellation
        // handler. Keep initiation and completion on one strand so fast children
        // cannot destroy the awaiting coroutine's cancellation state mid-launch.
        auto strand = asio::make_strand(co_await asio::this_coro::executor);
        co_await asio::co_spawn(strand, RunAllOnStrand(std::move(executor), std::move(tasks)), asio::use_awaitable);
    }
}
