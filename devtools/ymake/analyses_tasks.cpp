#include "analyses_tasks.h"

#include <devtools/ymake/libs/async/parallel.h>

asio::awaitable<TMaybe<EBuildResult>> RunConcurrentAnalyses(
    asio::any_io_executor executor,
    std::function<void()> checkIsolatedProjects,
    std::function<TMaybe<EBuildResult>()> otherAnalyses)
{
    TMaybe<EBuildResult> result;
    std::vector<std::function<asio::awaitable<void>()>> tasks;
    tasks.emplace_back([&]() -> asio::awaitable<void> {
        checkIsolatedProjects();
        co_return;
    });
    tasks.emplace_back([&]() -> asio::awaitable<void> {
        result = otherAnalyses();
        co_return;
    });
    co_await NYMake::RunAll(executor, std::move(tasks));
    co_return result;
}
