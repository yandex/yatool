#pragma once

#include <util/generic/hash.h>

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

template<>
struct THash<fs::path>: public THash<TStringBuf> {
    size_t operator()(const fs::path& val) const {
        return static_cast<const THash<TStringBuf>&>(*this)(TStringBuf{val.c_str()});
    }
};

template<>
struct TEqualTo<std::string> {
    template <class TOther>
    inline bool operator()(const std::string& a, const TOther& b) const {
        return a == b;
    }
};

namespace NYexport {
    fs::path ArcadiaSourceRootPath();
}
