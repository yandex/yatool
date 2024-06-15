#pragma once

#include <library/cpp/openssl/crypto/sha.h>

#include <util/folder/path.h>
#include <util/stream/input.h>
#include <util/string/hex.h>

namespace NACDigest {
    constexpr int DIGEST_GIT_LIKE_VERSION = 1;
    constexpr int DIGEST_CURRENT_VERSION = DIGEST_GIT_LIKE_VERSION;

    class TStreamDigest {
    public:
        using TValue = NOpenSsl::NSha1::TDigest;
        inline TStreamDigest(const TValue& value, size_t dataSize)
            : Value_{value}
            , DataSize_{dataSize}
        {
        }

        inline bool operator==(TStreamDigest other) const {
            return Value_ == other.Value_ && DataSize_ == other.DataSize_;
        }

        inline TString AsHexString() const {
            return to_lower(HexEncode(reinterpret_cast<const char*>(&Value_), NOpenSsl::NSha1::DIGEST_LENGTH));
        }

        inline size_t DataSize() const {
            return DataSize_;
        }
    private:
        TValue Value_{};
        size_t DataSize_{};
    };

    struct TFileDigest {
        TString ContentDigest;
        TString Uid;
        size_t Size;
    };

    TStreamDigest GetStreamDigest(IInputStream& in, size_t limit = 0);
    TFileDigest GetFileDigest(const TFsPath& fileName, TString contentDigest = {});
}
