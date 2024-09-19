#include "magic.h"

#include <cstring>

namespace NMagic {
    bool IsElf(const char* filename, const char* substr) {
        // We use libmagic instead of dlopen to determine is specified path is a dynlib to stay compatible with musl.

        const int isElfFlags = MAGIC_NO_CHECK_COMPRESS | MAGIC_NO_CHECK_TAR | MAGIC_NO_CHECK_JSON | MAGIC_NO_CHECK_CDF;
        bool isElf = false;

        struct magic_set* ms = magic_open(MAGIC_RAW | MAGIC_SYMLINK | isElfFlags);
        if (ms) {
            if (magic_load(ms, nullptr) == 0) {
                const char* text;
                if (text = magic_file(ms, filename)) {
                    if (strncmp(text, "ELF ", 4) == 0 && (substr == nullptr || strstr(text, substr))) {
                        isElf = true;
                    }
                }
            }
            magic_close(ms);
        }
        return isElf;
    }

    bool IsDynLib(const char* filename) {
        return IsElf(filename, " shared object");
    }

    bool IsElfExecutable(const char* filename) {
        return IsElf(filename, " executable");
    }
}
