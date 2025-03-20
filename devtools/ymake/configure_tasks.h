#pragma once

#include "ymake.h"

#include <asio/awaitable.hpp>

asio::awaitable<TMaybe<EBuildResult>> RunConfigureAsync(THolder<TYMake>& yMake, TConfigurationExecutor exec);
asio::awaitable<void> ProcessEvlogAsync(THolder<TYMake>& yMake, TBuildConfiguration& conf, IInputStream& input, TConfigurationExecutor exec);
