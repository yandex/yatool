#pragma once

#include <contrib/libs/xxhash/xxhash.h>

#include <util/folder/path.h>
#include <util/stream/input.h>
#include <util/string/hex.h>

namespace NACDigest {
    constexpr int DIGEST_GIT_LIKE_VERSION = 1;
    constexpr int DIGEST_XXHASH_VERSION = 2;
    constexpr int DIGEST_CURRENT_VERSION = DIGEST_XXHASH_VERSION;

    class TStreamDigest {
    public:
        using TValue = XXH128_hash_t;
        inline TStreamDigest(const TValue& value, size_t dataSize)
            : Value_{value}
            , DataSize_{dataSize}
        {
        }

        inline bool operator==(TStreamDigest other) const {
            return Value_.low64 == other.Value_.low64 && Value_.high64 == other.Value_.high64 && DataSize_ == other.DataSize_;
        }

        inline TString AsHexString() const {
            XXH128_canonical_t canonical{};
            XXH128_canonicalFromHash(&canonical, Value_);
            return to_lower(HexEncode(canonical.digest, sizeof(canonical.digest)));
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
    TString GetBufferDigest(const char* ptr, size_t size);
}
