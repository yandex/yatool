#include "extended_url.h"

#include <util/string/cast.h>
#include <library/cpp/string_utils/base64/base64.h>
#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/json/json_writer.h>

namespace NUniversalFetcher {

    NJson::TJsonValue TUrlParams::ToJson() const {
        NJson::TJsonValue json(NJson::JSON_MAP);
        if (RawIntegrity) {
            json["raw_integrity"] = (*RawIntegrity);
        }
        return json;
    }

    TString TUrlParams::ToJsonString() const {
        return NJson::WriteJson(ToJson());
    }

    std::pair<TString, TUrlParams> ParseUrlWithParams(const TString& extUrl) {
        TStringBuf tok(extUrl);
        TString url = TString(tok.NextTok('#'));
        TUrlParams params;
        while (tok) {
            auto kv = tok.NextTok('&');
            auto key = kv.NextTok('=');
            auto value = kv;

            if (key == "integrity") {
                params.RawIntegrity = value;
            } else {
                // TODO: maybe throw exception?
            }
        }

        return {url, params};
    }

    TChecksumInfo ParseChecksumInfo(const TString& integrity) {
        TStringBuf tok(integrity);
        EHashAlgorithm algorithm = FromString<EHashAlgorithm>(tok.NextTok("-"));
        TString hash = Base64StrictDecode(TString(tok));

        if (!IsDigestOk(algorithm, hash)) {
            ythrow yexception() << "Malformed integrity digest for " << ToString(algorithm);
        }

        return {
            .Algorithm = algorithm,
            .Digest = std::move(hash)
        };
    }

    TString CalcCacheKey(const TString& rawIntegrity) {
        //TODO(jolex007): calc key from parts that don't depend on base64 and url format (here and in the ya make)
        return MD5::Calc(rawIntegrity);
    }

}
