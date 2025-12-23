#include "acdigest.h"

#include <util/generic/buffer.h>
#include <util/stream/buffer.h>
#include <util/stream/file.h>
#include <util/stream/format.h>
#include <util/stream/str.h>
#include <util/system/error.h>
#include <util/system/fs.h>

namespace {
    class TXXHasher{
    public:
        TXXHasher() {
            State_ = XXH3_createState();
            Y_ENSURE(State_, "Out of memory");
            XXH3_128bits_reset(State_);
        }

        ~TXXHasher() {
            XXH3_freeState(State_);
        }

        void Update(const char* data, size_t size) {
            XXH3_128bits_update(State_, data, size);
        }

        XXH128_hash_t Final() {
            return XXH3_128bits_digest(State_);
        }
    private:
        XXH3_state_t* State_{};
    };
}

namespace NACDigest {
    TStreamDigest GetStreamDigest(IInputStream& in, size_t limit) {
        TXXHasher calc{};
        size_t totalSize = 0;
        TTempBuf buf;
        while (size_t size = in.Read(buf.Data(), buf.Size())) {
            if (limit) {
                size = Min(size, limit - totalSize);
            }
            totalSize += size;
            calc.Update(buf.Data(), size);

            if (totalSize == limit) {
                break;
            }
        }
        return TStreamDigest{calc.Final(), totalSize};
    }

    TFileDigest GetFileDigest(const TFsPath& fileName, TString contentDigest) {
        TFileStat stat{fileName, true /* no follow*/};
        if (stat.IsNull()) {
            int err = LastSystemError();
            ythrow TIoException() << "failed to stat " << fileName << ": " << LastSystemErrorText(err);
        }

        size_t size = 0;
        if (contentDigest.empty()) {
            THolder<IInputStream> input{};
            if (stat.IsSymlink()) {
                input = MakeHolder<TStringStream>(NFs::ReadLink(fileName));
            } else {
                input = MakeHolder<TFileInput>(fileName);
            }
            TStreamDigest digest = GetStreamDigest(*input);
            contentDigest = digest.AsHexString();
            size = digest.DataSize();
        }

        TStringStream ss;
        ss << contentDigest;
        ss << "mode: " << Hex(stat.Mode);
        return {contentDigest, GetStreamDigest(ss).AsHexString(), size};
    }

    TString GetBufferDigest(const char* ptr, size_t size) {
        TBuffer buffer{ptr, size};
        TBufferInput input{buffer};
        return NACDigest::GetStreamDigest(input).AsHexString();
    }
}
