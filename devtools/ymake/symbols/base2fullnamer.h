#pragma once

#include <util/string/type.h>

#include <string>

class TBase2FullNamer {
public:
    static constexpr const size_t MAX_BASENAME_LEN = 512;

    TBase2FullNamer();
    ~TBase2FullNamer() = default;

    void SetPath(const TStringBuf dirName);
    TStringBuf GetFullname(const TStringBuf basename);

private:
    std::string Fullname_;
    size_t PathLen_;
};
