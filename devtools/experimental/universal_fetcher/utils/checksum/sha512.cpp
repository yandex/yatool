#include "sha512.h"

#include <util/generic/yexception.h>

#include <openssl/sha.h>

namespace NOpenSsl {
    namespace NSha512 {
        static_assert(DIGEST_LENGTH == SHA512_DIGEST_LENGTH);

        TDigest Calc(const void* data, size_t dataSize) {
            TDigest digest;
            Y_ENSURE(SHA512(static_cast<const ui8*>(data), dataSize, digest.data()) != nullptr);
            return digest;
        }

        TCalcer::TCalcer()
            : Context{new SHA512state_st} {
            Y_ENSURE(SHA512_Init(Context.Get()) == 1);
        }

        TCalcer::~TCalcer() {
        }

        void TCalcer::Update(const void* data, size_t dataSize) {
            Y_ENSURE(SHA512_Update(Context.Get(), data, dataSize) == 1);
        }

        TDigest TCalcer::Final() {
            TDigest digest;
            Y_ENSURE(SHA512_Final(digest.data(), Context.Get()) == 1);
            return digest;
        }
    }
}
