#pragma once

#include "parallel.h"

#include <asio/co_spawn.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/use_awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>

namespace NYMake {
    struct TPipelineStats {
        double PreparationSeconds = 0;
        double RenderingSeconds = 0;
        double SendWaitSeconds = 0;
        double ReceiveWaitSeconds = 0;
        size_t Items = 0;
    };

    // Callbacks are synchronous. Neither queue waits nor joining blocks a worker.
    // A missing item ends the stream; failures close the queue and join both sides.
    template <class TItem, class TProduce, class TConsume>
    asio::awaitable<TPipelineStats> RunPipeline(asio::any_io_executor producerExecutor,
                                               asio::any_io_executor consumerExecutor,
                                               size_t capacity, TProduce produce, TConsume consume) {
        if (capacity == 0) {
            throw std::invalid_argument("Pipeline capacity must be positive");
        }
        using TClock = std::chrono::steady_clock;
        const auto elapsed = [](auto start) {
            return std::chrono::duration<double>(TClock::now() - start).count();
        };
        asio::experimental::concurrent_channel<void(asio::error_code, std::optional<TItem>)> queue(producerExecutor, capacity);
        asio::experimental::concurrent_channel<void(asio::error_code, int)> slots(producerExecutor, capacity);
        for (size_t i = 0; i < capacity; ++i) {
            slots.try_send(asio::error_code{}, 0);
        }
        const auto stop = [&] {
            slots.close();
            queue.close();
        };
        TPipelineStats stats;
        std::exception_ptr producerError;
        std::exception_ptr consumerError;
        const auto producer = [&]() -> asio::awaitable<void> {
            try {
                while (true) {
                    const auto wait = TClock::now();
                    co_await slots.async_receive(asio::use_awaitable);
                    stats.SendWaitSeconds += elapsed(wait);
                    const auto start = TClock::now();
                    std::optional<TItem> item;
                    try {
                        item = produce();
                    } catch (...) {
                        producerError = std::current_exception();
                        throw;
                    }
                    stats.PreparationSeconds += elapsed(start);
                    const bool done = !item;
                    if (!queue.try_send(asio::error_code{}, std::move(item))) {
                        throw asio::system_error(asio::experimental::error::channel_closed);
                    }
                    if (done) {
                        co_return;
                    }
                }
            } catch (...) {
                stop();
                throw;
            }
        };
        const auto consumer = [&]() -> asio::awaitable<void> {
            try {
                while (true) {
                    const auto wait = TClock::now();
                    auto item = co_await queue.async_receive(asio::use_awaitable);
                    stats.ReceiveWaitSeconds += elapsed(wait);
                    if (!item) {
                        co_return;
                    }
                    if (!slots.try_send(asio::error_code{}, 0)) {
                        throw asio::system_error(asio::experimental::error::channel_closed);
                    }
                    const auto start = TClock::now();
                    try {
                        consume(*item);
                    } catch (...) {
                        consumerError = std::current_exception();
                        throw;
                    }
                    stats.RenderingSeconds += elapsed(start);
                    ++stats.Items;
                }
            } catch (...) {
                stop();
                throw;
            }
        };
        std::vector<std::function<asio::awaitable<void>()>> tasks;
        tasks.emplace_back(producer);
        tasks.emplace_back([&]() -> asio::awaitable<void> {
            try {
                co_await asio::co_spawn(consumerExecutor, consumer(), asio::use_awaitable);
            } catch (...) {
                stop();
                throw;
            }
        });
        try {
            co_await RunAll(producerExecutor, std::move(tasks));
        } catch (...) {
            if (producerError) {
                std::rethrow_exception(producerError);
            }
            if (consumerError) {
                std::rethrow_exception(consumerError);
            }
            throw;
        }
        co_return stats;
    }
}
