#pragma once

#include <library/cpp/config/config.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/maybe.h>
#include <util/string/cast.h>

namespace NUserServicePrivate {
    enum EConfigStrings {
        GrpcServer /* "grpc" */,
        Address /* "address" */,
        Inet /* "inet" */,
        Inet6 /* "inet6" */,
        Local /* "local" */,
        ShutdownDealline /* "shutdown_call_deadline_ms" */,
        TermSignalWait /* "wait_term_s" */,
        LogNameStr /* "log" */,
    };

    inline TString IniSectionName() {
        return ToString(GrpcServer);
    }

    /// Options overriding values in ini-file.
    struct TConfigOptions {
        /// [grpc] shutdown_call_deadline_ms
        TMaybe<i64> GrpcShutdownDeadline;
        /// [grpc] shutdown_call_deadline_ms
        TMaybe<TString> GrpcAddress;
        /// [grpc] wait_term_s
        TMaybe<i64> GrpcTermWait;
        /// [grpc] log
        TMaybe<TString> LogName;
        bool HasParams() const;
        void WriteSection(IOutputStream& out) const;
        void AddOptions(NLastGetopt::TOpts& parser);
    };

    void CheckConfig(const NConfig::TConfig& config);

    void PrepareDirs(const NConfig::TConfig& config);

    TString GetLogName(const NConfig::TConfig& config);
}
