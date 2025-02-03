#pragma once

#include <util/generic/string.h>
#include <util/generic/hash.h>
#include <library/cpp/http/simple/http_client.h>

namespace NUniversalFetcher {

    void DoHttpGetRequest(const TString& url, IOutputStream* output, TRedirectableHttpClient::TOptions options = {}, const THashMap<TString, TString>& headers = {});

}
