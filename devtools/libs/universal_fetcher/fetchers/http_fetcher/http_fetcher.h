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
        // Response body ended before the announced Content-Length.
        virtual void OnFetchContentLengthMismatch() {};

        virtual ~IHttpFetcherMetrics() {
        }
    };

    struct THttpFetcherParams {
        TString UserAgent = "HttpUnifetcher/0.1";
        TDuration SocketTimeout = TDuration::Seconds(30);
        TDuration ConnectTimeout = TDuration::Seconds(30);
        size_t MaxRedirectCount = 5;
        // Treat a body that ends before Content-Length as a retriable error instead of a
        // successful short fetch. See THttpInput::TOptions::StrictContentLength.
        bool VerifyContentLength = true;

        static THttpFetcherParams FromJson(const NJson::TJsonValue&);
    };

    TFetcherPtr CreateHttpFetcher(const THttpFetcherParams&, IHttpFetcherMetrics* metrics = nullptr);

}
