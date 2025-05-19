#pragma once

#include "conf.h"
#include "context_executor.h"

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>

asio::awaitable<int> main_real(TBuildConfiguration& conf, TExecutorWithContext<TExecContext> exec);
