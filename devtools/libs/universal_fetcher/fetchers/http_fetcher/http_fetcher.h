#pragma once

#include <devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h>

namespace NJson {
    class TJsonValue;
}

namespace NUniversalFetcher {

    class IHttpFetcherMetrics {
    public:
        virtual void OnFetchTooManyRedirects() {};
        virtual void OnFetchCode(int) {};
        virtual void OnFetchOk() {};

        virtual ~IHttpFetcherMetrics() {
        }
    };

    struct THttpFetcherParams {
        TString UserAgent = "HttpUnifetcher/0.1";
        TDuration SocketTimeout = TDuration::Seconds(30);
        TDuration ConnectTimeout = TDuration::Seconds(30);
        size_t MaxRedirectCount = 5;

        static THttpFetcherParams FromJson(const NJson::TJsonValue&);
    };

    TFetcherPtr CreateHttpFetcher(const THttpFetcherParams&, IHttpFetcherMetrics* metrics = nullptr);

}
