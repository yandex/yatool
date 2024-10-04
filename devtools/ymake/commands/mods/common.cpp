#include "common.h"
#include <fmt/format.h>

void NCommands::TBasicModImpl::CheckArgCount(ssize_t count) const {
    if (Arity != 0) {
        if (count != Arity)
            throw std::runtime_error{fmt::format("Invalid number of arguments in {}, {} expected", ToString(Id), Arity)};
    }
}
