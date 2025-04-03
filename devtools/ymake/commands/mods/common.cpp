#include "common.h"
#include <fmt/format.h>

void NCommands::TBasicModImpl::CheckArgCount(ssize_t count) const {
    if (Arity != 0)
        if (count != Arity) [[unlikely]]
            FailArgCount(count, std::to_string(Arity));
}

void NCommands::TBasicModImpl::FailArgCount(ssize_t count, std::string_view expected) const {
    throw std::runtime_error{fmt::format("Invalid number of arguments in {}: {} provided, {} expected", ToString(Id), count, expected)};
}
