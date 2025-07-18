#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

class TYMake;

asio::awaitable<void> ExportJSON(TYMake& yMake, asio::any_io_executor exec);
