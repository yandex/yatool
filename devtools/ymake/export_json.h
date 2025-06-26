#pragma once

#include <asio/any_io_executor.hpp>

class TYMake;

void ExportJSON(TYMake& yMake, asio::any_io_executor exec);
