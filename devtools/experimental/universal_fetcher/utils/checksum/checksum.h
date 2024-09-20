#pragma once

#include <util/generic/maybe.h>
#include <library/cpp/json/json_value.h>

namespace NUniversalFetcher {
    enum class EHashAlgorithm {
        Md5 /* "md5" */,
        Sha1 /* "sha1" */,
        Sha512 /* "sha512" */,
    };

    struct TChecksumInfo {
        EHashAlgorithm Algorithm;
        TString Digest;

        void ToJson(NJson::TJsonValue&) const;
        TString ToJsonString() const;
    };

    bool IsDigestOk(EHashAlgorithm algo, const TString& data);

    TString CalcContentDigest(EHashAlgorithm algo, const TString& filepath);

}
