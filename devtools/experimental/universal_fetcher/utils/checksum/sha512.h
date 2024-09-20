#pragma once

#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <util/system/types.h>

#include <array>

struct SHAstate_st;
struct SHA256state_st;
struct SHA512state_st;

namespace NOpenSsl::NSha512 {
    constexpr size_t DIGEST_LENGTH = 64;
    using TDigest = std::array<ui8, DIGEST_LENGTH>;

    // not fragmented input
    TDigest Calc(const void* data, size_t dataSize);

    inline TDigest Calc(TStringBuf s) {
        return Calc(s.data(), s.length());
    }

    // fragmented input
    class TCalcer {
    public:
        TCalcer();
        ~TCalcer();
        void Update(const void* data, size_t dataSize);

        void Update(TStringBuf s) {
            Update(s.data(), s.length());
        }

        template <typename T>
        void UpdateWithPodValue(const T& value) {
            Update(&value, sizeof(value));
        }

        TDigest Final();

    private:
        THolder<SHA512state_st> Context;
    };
}
