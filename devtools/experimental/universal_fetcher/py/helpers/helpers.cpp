#include "helpers.h"

#include <functional>

namespace NUniversalFetcher {

    std::function<void(ELogPriority, const TString&)> WrapPythonLogFunction(void* pyLogFunc, void(*pyLogFuncProxy)(void*, i32, const TString&)) {
        return [=](ELogPriority priority, const TString& msg) {
            pyLogFuncProxy(pyLogFunc, static_cast<i32>(priority), msg);
        };
    }

    std::pair<TString, TMaybe<TChecksumInfo>> ParseIntegrity(const TString& extUrl) {
        auto [url, params] = ParseUrlWithParams(extUrl);
        if (params.RawIntegrity.Defined()) {
            return {url, ParseChecksumInfo(*params.RawIntegrity)};
        } else {
            return {url, {}};
        }
    }

    bool CheckIntegrity(const TString& filepath, const TChecksumInfo& integrity) {
        Cout << filepath << Endl;
        TString digest = CalcContentDigest(integrity.Algorithm, filepath);
        return digest == integrity.Digest;
    }
}
