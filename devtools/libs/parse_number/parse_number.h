#pragma once

#include <util/generic/fwd.h>


namespace NDistBuild {

    /// Parse real number with respect to International System of Units prefixes; examples:
    ///
    /// `"1"` as `1`
    /// `"0.1"` as `0.1`
    /// `"100m"` as `0.1`
    /// `"100c"` as `1`
    /// `"100d"` as `10`
    bool TryParseHumanReadableNumber(TStringBuf str, float* number) noexcept;

    /// Parse integer number with respect to either International System of Units prefixes or binary prefixes; examples:
    ///
    /// `"1"` as `1`
    /// `"1KiB"` as `1024`
    /// `"1G"` as `1000`
    ///
    /// NOTE: fractional numbers will be rounded up to the nearest integer.
    bool TryParseHumanReadableNumber(TStringBuf str, ui64* number) noexcept;
};
