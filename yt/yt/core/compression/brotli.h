#pragma once

#include "stream.h"

namespace NYT::NCompression::NDetail {

////////////////////////////////////////////////////////////////////////////////

void BrotliCompress(int level, TSource* source, TBlob* output);
void BrotliDecompress(TSource* source, TBlob* output);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCompression::NDetail
