#pragma once

#include <devtools/ymake/lang/call_signature.h>

struct IMemoryPool;

size_t ConvertArgsToPositionalArrays(const TSignature& cmdProp, TVector<TStringBuf>& args, IMemoryPool& sspool);
