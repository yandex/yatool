#pragma once

#include <util/stream/output.h>

class TDelimiter {
public:
    TDelimiter(const char* delim) : Delim_(delim), First_(true) {}

    void Out(IOutputStream& out) {
        if (!First_) {
            out << Delim_;
        } else {
            First_ = false;
        }
    }

private:
    const char* Delim_;
    bool First_;
};

inline IOutputStream& operator<<(IOutputStream& out, TDelimiter& delim) {
    delim.Out(out);
    return out;
}

inline char BoolToChar(bool value) {
    return value ? 'Y' : 'N';
}

template<typename TEnum>
size_t MaxEnumValueLength() {
    size_t maxLength = 0;
    for (TEnum value : GetEnumAllValues<TEnum>()) {
        maxLength = Max(maxLength, ToString(value).Size());
    }
    return maxLength;
}
