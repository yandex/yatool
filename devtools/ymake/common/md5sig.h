#pragma once

#include <library/cpp/string_utils/base64/base64.h>

#include <util/system/defaults.h>
#include <util/generic/string.h>
#include <util/ysaveload.h>

// TODO: move to util
struct TMd5Sig {
    union {
        unsigned char RawData[16];
        struct {
            ui64 D0;
            ui64 D1;
        };
    };

    TMd5Sig()
        : D0()
        , D1()
    {
    }

    bool operator==(const TMd5Sig& to) const {
        return D0 == to.D0 && D1 == to.D1;
    }
    bool operator!=(const TMd5Sig& to) const {
        return D0 != to.D0 || D1 != to.D1;
    }
    bool operator<(const TMd5Sig& than) const { // like little-endian
        return D1 < than.D1 || D1 == than.D1 && D0 != than.D0;
    }

    Y_SAVELOAD_DEFINE(RawData);
};

inline TString Md5SignatureAsBase64(const TMd5Sig& s) {
    TString r = TString::Uninitialized(22);
    Base64EncodeUrlNoPadding(r.begin(), s.RawData, sizeof(s.RawData));
    return r;
}
