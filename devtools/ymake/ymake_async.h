#pragma once

#include "ymake.h"
#include "asio_extern_templates.h"

#include <asio/experimental/promise.hpp>
#include <asio/thread_pool.hpp>

struct TAsyncState {
    std::optional<asio::experimental::promise<void(std::exception_ptr, THolder<TMakePlanCache>)>> JSONCachePreloadingPromise;
    std::optional<asio::experimental::promise<void(std::exception_ptr, THolder<TUidsData>)>> UidsCachePreloadingPromise;
};
