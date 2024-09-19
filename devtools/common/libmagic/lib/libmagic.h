#pragma once

namespace NMagic {
    bool IsDynLib(const char* filename);
    bool IsElfExecutable(const char* filename);
    bool IsElf(const char* filename, const char* substr = nullptr);
}
