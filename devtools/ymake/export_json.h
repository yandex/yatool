#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <functional>

class TYMake;

using TJSONPostVisit = std::function<asio::awaitable<void>()>;

asio::awaitable<void> ExportJSON(TYMake& yMake, asio::any_io_executor exec, TJSONPostVisit afterVisit = {}, TJSONPostVisit alongsideTail = {});

asio::awaitable<bool> ExportJSONAndTestDart(TYMake& yMake, asio::any_io_executor exec, TJSONPostVisit saveCoreCaches);
