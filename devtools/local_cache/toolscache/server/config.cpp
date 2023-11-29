#include "server.h"

#include "devtools/local_cache/psingleton/server/server.h"

#include <library/cpp/resource/resource.h>

#include <util/draft/date.h>
#include <util/draft/datetime.h>
#include <util/folder/dirut.h>
#include <util/stream/file.h>
#include <util/system/file.h>
#include <util/system/env.h>

#include <stdlib.h>

using namespace NConfig;

TString NToolsCache::GetMiscRoot() {
    auto home = GetHomeDir();
    auto yaCache = GetEnv("YA_CACHE_DIR");
    return yaCache.Empty() ? JoinFsPaths(home, ".ya") : yaCache;
}

TString NToolsCache::GetToolDir() {
    auto yaToolCache = GetEnv("YA_CACHE_DIR_TOOLS");
    return JoinFsPaths(yaToolCache.Empty()
                           ? JoinFsPaths(GetMiscRoot(), "tools")
                           : yaToolCache,
                       "v4");
}

TString NToolsCache::GetCacheRoot() {
    return JoinFsPaths(GetMiscRoot(), "build", "cache", "7");
}

bool NToolsCacheServerPrivate::TConfigOptions::HasParams() const {
    return !ServiceList.Empty();
}

void NToolsCacheServerPrivate::TConfigOptions::WriteSection(IOutputStream& out) const {
    if (!HasParams()) {
        return;
    }
    using namespace NUserServicePrivate;
    out << "[" << IniSectionName() << "]" << Endl;
    if (auto* val = ServiceList.Get()) {
        out << ToString(ServiceStr) << "=" << *val << Endl;
    }
}

void NToolsCacheServerPrivate::TConfigOptions::AddOptions(NLastGetopt::TOpts& parser) {
    parser.AddLongOption("grpc-service", "[grpc] service")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&ServiceList);
}

static TMaybe<TString> GetServiceOption(const NConfig::TConfig& config) {
    using namespace NConfig;
    using namespace NToolsCacheServerPrivate;
    auto section = config.Get<TDict>().contains(ToString(GrpcServer)) ? config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>() : TDict();
    if (section.contains(ToString(ServiceStr))) {
        return section.At(ToString(ServiceStr)).Get<TString>();
    }
    return TMaybe<TString>();
}

void NToolsCacheServerPrivate::CheckConfig(const NConfig::TConfig& config) {
    auto service = GetServiceOption(config);
    if (service.Empty()) {
        return;
    }

    if (service != ToString(AllStr) && service != ToString(RunTCStr) && service != ToString(RunACStr)) {
        ythrow TTypeMismatch() << "'service' field can be 'all', 'build_cache' or 'tools_cache";
    }
}

bool NToolsCacheServerPrivate::DoRunService(const NConfig::TConfig& config, NToolsCacheServerPrivate::EConfigStrings e) {
    using namespace NConfig;
    auto service = GetServiceOption(config);
    if (!service.Empty()) {
        return service == ToString(AllStr) || service == ToString(e);
    }
    return e == RunTCStr;
}

TString NToolsCacheServerPrivate::GetServerLockName(const NConfig::TConfig& config) {
    if (DoRunService(config, RunTCStr)) {
        return NToolsCachePrivate::GetLockName(config);
    }
    return NACCachePrivate::GetLockName(config);
}

NConfig::TConfig NToolsCache::ReadConfig(TString fileName, const TConfigOptions& opts) {
    TStringStream sstr;
    sstr << "; Default parameters" << Endl;
    sstr << NResource::Find("tc/ini-file") << Endl;
    sstr << NResource::Find("ac/ini-file") << Endl;
    try {
        TFile ini(fileName, OpenExisting | RdOnly);
        if (ini.GetLength() > 100000) {
            ythrow TSystemError() << "Ini-file '" << fileName << "' is malformed, it is too big";
        }
        sstr << "; Ini-file" << Endl;
        sstr << TUnbufferedFileInput(ini).ReadAll() << Endl;
    } catch (const TIoException& e) {
        fileName = "<devtools/local_cache/toolscache/server/tc.ini>";
    }

    sstr << "; Parameters from command line" << Endl;
    opts.GrpcParams.WriteSection(sstr);
    opts.ServerParams.WriteSection(sstr);
    opts.TcParams.WriteSection(sstr);
    opts.AcParams.WriteSection(sstr);

    TConfig config;
    try {
        TGlobals g;
        auto home = GetHomeDir();
        g["home"] = home;
        g["procid"] = TProcessUID::GetMyUniqueSuffix();
        g["date"] = TDate::Today().ToStroka("%Y-%m-%d");
        g["time"] = NDatetime::TSimpleTM::NewLocal(NDatetime::TSimpleTM::CurrentUTC().AsTimeT()).ToString("%H-%M-%S");
        g["misc_root"] = GetMiscRoot();
        g["tool_root"] = GetToolDir();
        g["cache_root"] = GetCacheRoot();
        config = TConfig::ReadIni(sstr.Str(), g);
    } catch (const TConfigParseError&) {
        Cerr << "Cannot parse ini file '" << fileName << "'" << Endl;
        throw;
    }
    return config;
}

TString GetPersistentDirectory(const NConfig::TConfig& config) {
    auto lockFile = NToolsCacheServerPrivate::GetServerLockName(config);
    return TFsPath(lockFile).Dirname();
}

TString GetCriticalErrorMarkerFileName(const NConfig::TConfig& config) {
    // Only AC cache permits rebuilding.
    if (NToolsCacheServerPrivate::DoRunService(config, NToolsCacheServerPrivate::RunACStr)) {
        return NACCachePrivate::GetCriticalErrorMarkerFileName(config);
    }

    return "";
}
