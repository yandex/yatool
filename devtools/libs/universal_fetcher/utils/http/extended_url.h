#pragma once

#include <util/generic/string.h>
#include <util/generic/hash.h>
#include <library/cpp/json/json_value.h>
#include <devtools/libs/universal_fetcher/utils/checksum/checksum.h>

namespace NUniversalFetcher {

    struct TUrlParams {
        TMaybe<TString> RawIntegrity;

        NJson::TJsonValue ToJson() const;
        TString ToJsonString() const;
    };

    std::pair<TString, TUrlParams> ParseUrlWithParams(const TString& extUrl);
    TChecksumInfo ParseChecksumInfo(const TString& rawIntegrity);
    TString CalcCacheKey(const TString& rawIntegrity);

}
