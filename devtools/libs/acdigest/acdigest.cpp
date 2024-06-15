#include "acdigest.h"

#include <util/stream/file.h>
#include <util/stream/format.h>
#include <util/stream/str.h>
#include <util/system/error.h>
#include <util/system/fs.h>

namespace NACDigest {
    TStreamDigest GetStreamDigest(IInputStream& in, size_t limit) {
        NOpenSsl::NSha1::TCalcer calc{};
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

        if (limit == 0) {
            // Add size to hash
            calc.Update("", 1);
            TString fileSize{ToString(totalSize)};
            calc.Update(fileSize.Data(), fileSize.Size());
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
        if (contentDigest.Empty()) {
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
}
