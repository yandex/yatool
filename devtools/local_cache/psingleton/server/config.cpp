#include "config.h"

#include <util/folder/path.h>

bool NUserServicePrivate::TConfigOptions::HasParams() const {
    return !LogName.Empty() || !GrpcShutdownDeadline.Empty() || !GrpcTermWait.Empty() || !GrpcAddress.Empty();
}

void NUserServicePrivate::TConfigOptions::WriteSection(IOutputStream& out) const {
    if (!HasParams()) {
        return;
    }
    using namespace NUserServicePrivate;
    out << "[" << IniSectionName() << "]" << Endl;
    if (auto* val = LogName.Get()) {
        out << ToString(LogNameStr) << "=" << *val << Endl;
    }
    if (auto* val = GrpcShutdownDeadline.Get()) {
        out << ToString(ShutdownDealline) << "=" << *val << Endl;
    }
    if (auto* val = GrpcTermWait.Get()) {
        out << ToString(TermSignalWait) << "=" << *val << Endl;
    }
    if (auto* val = GrpcAddress.Get()) {
        out << ToString(Address) << "=" << *val << Endl;
    }
}

void NUserServicePrivate::TConfigOptions::AddOptions(NLastGetopt::TOpts& parser) {
    parser.AddLongOption("grpc-shutdown_call_deadline_ms", "[grpc] shutdown_call_deadline_ms")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&GrpcShutdownDeadline);
    parser.AddLongOption("grpc-wait_term_s", "[grpc] wait_term_s")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&GrpcTermWait);
    parser.AddLongOption("grpc-address", "[grpc] address")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&GrpcAddress);
    parser.AddLongOption("grpc-log", "[grpc] log")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&LogName);
}

static void MalformedEntryError(NUserServicePrivate::EConfigStrings e) {
    Cerr << "Malformed '" << ToString(e) << "' entry in [" << NUserServicePrivate::IniSectionName() << "] section" << Endl;
}

// static void MissingEntryError(NUserServicePrivate::EConfigStrings e) {
//     Cerr << "Missing '" << ToString(e) << "' entry in [" << NUserServicePrivate::IniSectionName() << "] section" << Endl;
// }

static bool CheckFsPathParameter(const TString& s, NUserServicePrivate::EConfigStrings e) {
    using namespace NUserServicePrivate;
    switch (e) {
        case LogNameStr:
            return TFsPath(s).IsAbsolute() || s == "";
        default:
            break;
    }
    return false;
}

void NUserServicePrivate::CheckConfig(const NConfig::TConfig& config) {
    using namespace NConfig;
    auto section = config.Get<TDict>().contains(ToString(GrpcServer)) ? config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>() : TDict();

    if (section.contains(ToString(Address))) {
        auto address = section.At(ToString(Address)).Get<TString>();
        if (address != ToString(Inet) && address != ToString(Inet6) && address != ToString(Local)) {
            ythrow TTypeMismatch() << "'address' field can be 'inet', 'inet6' or 'local";
        }
    }

    // i64
    for (auto e : {ShutdownDealline, TermSignalWait}) {
        try {
            if (section.contains(ToString(e))) {
                auto s = section.At(ToString(e)).As<i64>();
                if (s < 0) {
                    ythrow TTypeMismatch() << "Negative value is not expected";
                }
            }
        } catch (const TTypeMismatch&) {
            ythrow TTypeMismatch() << section.At(ToString(e)).Get<TString>();
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // TString + TFsPath
    for (auto e : {LogNameStr}) {
        try {
            if (section.contains(ToString(e))) {
                auto& s = section.At(ToString(e)).Get<TString>();
                if (!CheckFsPathParameter(s, e)) {
                    ythrow TTypeMismatch() << "'" << ToString(e) << "' '" << s << "' should be absolute path";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        }
    }
}

void NUserServicePrivate::PrepareDirs(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& section = config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>();

    for (auto e : {LogNameStr}) {
        if (section.contains(ToString(e))) {
            if (auto file = section.At(ToString(e)).Get<TString>(); !file.Empty()) {
                TFsPath(TFsPath(file).Dirname()).MkDirs(0755);
            }
        }
    }
}

TString NUserServicePrivate::GetLogName(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& tcSection = config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>();

    if (tcSection.contains(ToString(LogNameStr))) {
        return tcSection.At(ToString(LogNameStr)).Get<TString>();
    }

    return "";
}
