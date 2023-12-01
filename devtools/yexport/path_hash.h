#pragma once

#include <util/generic/hash.h>

#include <filesystem>

namespace fs = std::filesystem;

template<>
struct THash<fs::path>: public THash<TStringBuf> {
    size_t operator()(const fs::path& val) const {
        return static_cast<const THash<TStringBuf>&>(*this)(TStringBuf{val.c_str()});
    }
};
