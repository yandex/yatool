#pragma once

#include "conf.h"

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>

asio::awaitable<int> main_real(TBuildConfiguration& conf, asio::thread_pool::executor_type exec);
