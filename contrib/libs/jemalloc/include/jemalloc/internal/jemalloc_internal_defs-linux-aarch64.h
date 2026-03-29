#pragma once

#include "jemalloc_internal_defs-linux.h"

#undef LG_PAGE
#undef LG_HUGEPAGE

// 64 KiB page size is compatible with 4, 16, 64 KiB
#define LG_PAGE 16

// 2 MiB huge pages are available in all modes
#define LG_HUGEPAGE 21
