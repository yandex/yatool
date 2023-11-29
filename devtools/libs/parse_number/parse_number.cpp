#include "parse_number.h"

#include <util/string/cast.h>
#include <util/generic/vector.h>

#include <cmath>

namespace NDistBuild {

    bool TryParseHumanReadableNumber(TStringBuf str, float* number) noexcept {
        if (str.Empty()) {
            return false;
        }

        static const std::pair<TStringBuf, long double> suffixes[] = {
            {"y", 0.000000000000000000000001},
            {"z", 0.000000000000000000001},
            {"a", 0.000000000000000001},
            {"f", 0.000000000000001},
            {"p", 0.000000000001},
            {"n", 0.000000001},
            {"u", 0.000001},
            {"m", 0.001},
            {"c", 0.01},
            {"d", 0.1},
            {"da", 10},
            {"h", 100},
            {"k", 1000},
            {"M", 1000000},
            {"G", 1000000000},
            {"T", 1000000000000},
            {"P", 1000000000000000},
            {"E", 1000000000000000000},
            {"Z", 1000000000000000000000.0},
            {"Y", 1000000000000000000000000.0},
            {"", 1},
        };

        for (const auto& [suffix, factor]: suffixes) {
            if (!str.EndsWith(suffix)) {
                continue;
            }

            const auto prefix = str.substr(0, str.size() - suffix.size());
            if (long double real = 0; TryFromString(prefix, real)) {
                *number = real * factor;
                return true;
            }
        }

        return false;
    }

    bool TryParseHumanReadableNumber(TStringBuf str, ui64* number) noexcept {
        if (str.Empty()) {
            return false;
        }

        static const std::pair<TStringBuf, ui64> suffixes[] = {
            {"Ei", 1024ULL * 1024 * 1024 * 1024 * 1024 * 1024},
            {"Pi", 1024UL * 1024 * 1024 * 1024 * 1024},
            {"Ti", 1024UL * 1024 * 1024 * 1024},
            {"Gi", 1024 * 1024 * 1024},
            {"Mi", 1024 * 1024},
            {"Ki", 1024},
            {"da", 10},
            {"h", 100},
            {"k", 1000},
            {"M", 1000 * 1000},
            {"G", 1000 * 1000 * 1000},
            {"T", 1000UL * 1000 * 1000 * 1000},
            {"P", 1000UL * 1000 * 1000 * 1000 * 1000},
            {"E", 1000ULL * 1000 * 1000 * 1000 * 1000 * 1000},
            {"", 1},
        };

        for (const auto& [suffix, factor]: suffixes) {
            if (!str.EndsWith(suffix)) {
                continue;
            }

            const auto prefix = str.substr(0, str.size() - suffix.size());
            if (ui64 integer = 0; TryFromString(prefix, integer)) {
                *number = integer * factor;
                return true;
            } else if (long double real = 0; TryFromString(prefix, real)) {
                *number = ceil(real * factor);
                return true;
            }
        }
        return false;
    }
};
