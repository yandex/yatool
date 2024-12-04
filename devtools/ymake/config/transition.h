#pragma once

#include <util/system/types.h>

enum class ETransition : ui8 {
    None = 0,
    Tool /* "tool" */,
    Pic /* "pic" */,
    NoPic /* "nopic" */,
};
