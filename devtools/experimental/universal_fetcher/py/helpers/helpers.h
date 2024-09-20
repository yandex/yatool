#pragma once

#include <util/generic/string.h>
#include <library/cpp/logger/priority.h>
#include <devtools/experimental/universal_fetcher/utils/http/extended_url.h>

namespace NUniversalFetcher {

    std::function<void(ELogPriority, const TString&)> WrapPythonLogFunction(void* pyLogFunc, void(*pyLogFuncProxy)(void*, i32, const TString&));

    std::pair<TString, TMaybe<TChecksumInfo>> ParseIntegrity(const TString& extUrl);
    bool CheckIntegrity(const TString& filepath, const TChecksumInfo& integrity);

}
