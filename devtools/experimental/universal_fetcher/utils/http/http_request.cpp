#include "http_request.h"

#include <library/cpp/uri/uri.h>
#include <util/generic/yexception.h>
#include <util/stream/file.h>

namespace NUniversalFetcher {

    void DoHttpGetRequest(const TString& url, IOutputStream* output, TRedirectableHttpClient::TOptions options, const THashMap<TString, TString>& headers) {
        NUri::TUri parsed;
        if (parsed.Parse(url, NUri::TUri::NewFeaturesRecommended) != NUri::TUri::EParsed::ParsedOK) {
            ythrow yexception() << "Failed to parse url";
        }
        auto schemeHost = parsed.PrintS(NUri::TUri::FlagScheme | NUri::TUri::EFlags::FlagHost);

        options.Host(schemeHost);
        options.Port(parsed.GetPort());

        TRedirectableHttpClient client(options);
        auto relUrl = parsed.PrintS(NUri::TUri::FlagPath | NUri::TUri::EFlags::FlagQuery);
        
        client.DoGet(relUrl, output, headers);
    }

}
