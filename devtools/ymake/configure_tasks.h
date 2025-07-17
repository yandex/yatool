#pragma once

#include "ymake.h"

#include <asio/awaitable.hpp>
#include <devtools/ymake/foreign_platforms/pipeline.h>

asio::awaitable<TMaybe<EBuildResult>> RunConfigureAsync(THolder<TYMake>& yMake, TConfigurationExecutor exec);
asio::awaitable<void> ProcessEvlogAsync(THolder<TYMake>& yMake, TBuildConfiguration& conf, NForeignTargetPipeline::TLineReader& input, TConfigurationExecutor exec);
