#pragma once

#include <library/cpp/config/config.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/maybe.h>
#include <util/string/cast.h>

namespace NToolsCacheServerPrivate {
    enum EConfigStrings {
        GrpcServer /* "grpc" */,
        ServiceStr /* "service" */,
        AllStr /* "all" */,
        RunTCStr /* "tools_cache" */,
        RunACStr /* "build_cache" */,
    };

    inline TString IniSectionName() {
        return ToString(GrpcServer);
    }

    /// Options overriding values in ini-file.
    struct TConfigOptions {
        TMaybe<TString> ServiceList;
        bool HasParams() const;
        void WriteSection(IOutputStream& out) const;
        void AddOptions(NLastGetopt::TOpts& parser);
    };

    void CheckConfig(const NConfig::TConfig& config);

    bool DoRunService(const NConfig::TConfig& config, EConfigStrings e);

    TString GetServerLockName(const NConfig::TConfig& config);
}
