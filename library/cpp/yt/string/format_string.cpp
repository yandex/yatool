#include "format_string.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

TRuntimeFormat::TRuntimeFormat(TStringBuf fmt)
    : Format_(fmt)
{ }

TStringBuf TRuntimeFormat::Get() const noexcept
{
    return Format_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT