#include "checksum.h"

#include "sha512.h"

#include <util/system/file.h>
#include <util/stream/file.h>
#include <util/string/cast.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/string_utils/base64/base64.h>
#include <library/cpp/openssl/crypto/sha.h>
#include <library/cpp/json/json_writer.h>

namespace NUniversalFetcher {

    void TChecksumInfo::ToJson(NJson::TJsonValue& ret) const {
        ret["algorithm"] = ToString(Algorithm);
        ret["value"] = Digest;
    }

    TString TChecksumInfo::ToJsonString() const {
        NJson::TJsonValue json;
        ToJson(json);
        return NJson::WriteJson(json);
    }

    bool IsDigestOk(EHashAlgorithm algo, const TString& data) {
        switch (algo) {
        case EHashAlgorithm::Md5:
            return data.size() == 16;
        case EHashAlgorithm::Sha1:
            return data.size() == NOpenSsl::NSha1::DIGEST_LENGTH;
        case EHashAlgorithm::Sha512:
            return data.size() == NOpenSsl::NSha512::DIGEST_LENGTH;
        }
    }

    TString GetMd5Hash(MD5& md5) {
        TString result = TString::TUninitialized(33);
        md5.End(result.begin());
        result.erase(32);
        return result;
    }

    TString CalcContentDigest(EHashAlgorithm algo, const TString& filepath) {
        const TFile file(filepath, OpenExisting | RdOnly | CloseOnExec);
        TString data = TFileInput(file).ReadAll();

        switch (algo) {
        case EHashAlgorithm::Md5:
            return MD5::CalcRaw(data);
        case EHashAlgorithm::Sha1: {
            // TODO(trofimenkov): Better cast
            auto d = NOpenSsl::NSha1::Calc(data);
            return TString((const char*)(&d[0]), NOpenSsl::NSha1::DIGEST_LENGTH);
        }
        case EHashAlgorithm::Sha512: {
            auto d = NOpenSsl::NSha512::Calc(data);
            return TString((const char*)(&d[0]), NOpenSsl::NSha512::DIGEST_LENGTH);
        }
        }
    }

}
