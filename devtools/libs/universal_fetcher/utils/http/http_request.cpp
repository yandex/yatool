#include "http_request.h"

#include <util/generic/yexception.h>
#include <util/stream/file.h>

namespace NUniversalFetcher {

    void DoHttpGetRequest(const TString& url, IOutputStream* output, TRedirectableHttpClient::TOptions options, const THashMap<TString, TString>& headers, THttpHeaders* outHeaders, NThreading::TCancellationToken cancellation) {
        TStringBuf schemeHostPort = GetSchemeHostAndPort(url);
        TStringBuf scheme("");
        TStringBuf host("unknown");
        ui16 port = 0;
        GetSchemeHostAndPort(schemeHostPort, scheme, host, port);
        TStringBuf relUrl = GetPathAndQuery(url, false);

        if (port == 0) {
            if (scheme.StartsWith("https")) {
                port = 443;
            } else if (scheme.StartsWith("http")) {
                port = 80;
            } else {
                port = 80;
            }
        }

        options.Host(TString(scheme) + TString(host));
        options.Port(port);
        options.UseKeepAlive(false);
        options.UseConnectionPool(true);

        TRedirectableHttpClient client(options);
        client.DoGet(relUrl, output, headers, outHeaders, std::move(cancellation));
    }

}
