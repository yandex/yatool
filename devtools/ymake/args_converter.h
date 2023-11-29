#pragma once

#include "cmd_properties.h"

struct IMemoryPool;

size_t ConvertArgsToPositionalArrays(const TCmdProperty& cmdProp, TVector<TStringBuf>& args, IMemoryPool& sspool);
