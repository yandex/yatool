#pragma once

#include <util/generic/string.h>
#include <library/cpp/logger/priority.h>
#include <devtools/libs/universal_fetcher/utils/http/extended_url.h>

namespace NUniversalFetcher {

    std::function<void(ELogPriority, const TString&)> WrapPythonLogFunction(void* pyLogFunc, void(*pyLogFuncProxy)(void*, i32, const TString&));
    std::function<void(ui64, ui64)> WrapPythonProgressFunction(void* pyProgessFunc, void(*pyProgressFuncProxy)(void*, ui64, ui64));

    std::pair<TString, TMaybe<TChecksumInfo>> ParseIntegrity(const TString& extUrl);
    bool CheckIntegrity(const TString& filepath, const TChecksumInfo& integrity);

}
