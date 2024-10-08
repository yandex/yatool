#include "config.h"

#include <util/folder/path.h>

bool NToolsCachePrivate::TConfigOptions::HasParams() const {
    return LockFile != "" || !LogName.Empty() || DBPath != "" ||
           !MasterMode.Empty() || !DiskLimit.Empty() || !QuiescenceTime.Empty() ||
           !Version.Empty() || SbId != "" || SbAltId != "" || SbPath != "";
}

void NToolsCachePrivate::TConfigOptions::WriteSection(IOutputStream& out) const {
    if (!HasParams()) {
        return;
    }

    out << "[" << IniSectionName() << "]" << Endl;

    if (!LockFile.empty()) {
        out << ToString(LockFileStr) << "=" << LockFile << Endl;
    }
    if (auto* val = LogName.Get()) {
        out << ToString(LogNameStr) << "=" << *val << Endl;
    }
    if (!DBPath.empty()) {
        out << ToString(DbPathStr) << "=" << DBPath << Endl;
    }
    if (auto* val = MasterMode.Get()) {
        out << ToString(MasterModeStr) << "=" << *val << Endl;
    }
    if (auto* val = DiskLimit.Get()) {
        out << ToString(GcLimitStr) << "=" << *val << Endl;
    }
    if (auto* val = QuiescenceTime.Get()) {
        out << ToString(QuiescenceStr) << "=" << *val << Endl;
    }
    if (auto* val = Version.Get()) {
        out << ToString(VersionStr) << "=" << *val << Endl;
    }
    if (!SbId.empty()) {
        out << ToString(SandBoxIdStr) << "=" << SbId << Endl;
    }
    if (!SbAltId.empty()) {
        out << ToString(SandBoxAltIdStr) << "=" << SbAltId << Endl;
    }
    if (!SbPath.empty()) {
        out << ToString(SandBoxPathStr) << "=" << SbPath << Endl;
    }
}

void NToolsCachePrivate::TConfigOptions::AddOptions(NLastGetopt::TOpts& parser) {
    parser.AddLongOption("tools_cache-lock_file", "[local_cache.tools_cache] lock_file")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&LockFile);
    parser.AddLongOption("tools_cache-log", "[local_cache.tools_cache] log")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&LogName);
    parser.AddLongOption("tools_cache-db_path", "[local_cache.tools_cache] db_path")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&DBPath);
    parser.AddLongOption("tools_cache-master_mode", "[local_cache.tools_cache] master_mode")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&MasterMode);
    parser.AddLongOption("tools_cache-disk_limit", "[local_cache.tools_cache] disk_limit")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DiskLimit);
    parser.AddLongOption("tools_cache-quiescence_time", "[local_cache.tools_cache] quiescence_time")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&QuiescenceTime);
    parser.AddLongOption("tools_cache-version", "[local_cache.tools_cache] version")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<int>(&Version);
    parser.AddLongOption("tools_cache-sb_id", "[local_cache.tools_cache] sb_id")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&SbId);
    parser.AddLongOption("tools_cache-sb_alt_id", "[local_cache.tools_cache] sb_alt_id")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&SbAltId);
    parser.AddLongOption("tools_cache-sb_path", "[local_cache.tools_cache] sb_path")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&SbPath);
}

static void MalformedEntryError(NToolsCachePrivate::EConfigStrings e) {
    Cerr << "Malformed '" << ToString(e) << "' entry in [" << NToolsCachePrivate::IniSectionName() << "] section" << Endl;
}

static void MissingEntryError(NToolsCachePrivate::EConfigStrings e) {
    Cerr << "Missing '" << ToString(e) << "' entry in [" << NToolsCachePrivate::IniSectionName() << "] section" << Endl;
}

static bool CheckFsPathParameter(const TString& s, NToolsCachePrivate::EConfigStrings e) {
    using namespace NToolsCachePrivate;
    switch (e) {
        case LockFileStr:
            return TFsPath(s).IsAbsolute();
        case DbPathStr: {
            if (s.StartsWith("file:")) {
                TStringBuf sbuf(s);
                TStringBuf scheme, path;
                sbuf.Split(':', scheme, path);
                return TFsPath(path).IsAbsolute();
            }
            return TFsPath(s).IsAbsolute();
        }
        case LogNameStr:
            return TFsPath(s).IsAbsolute() || s == "";
        case SandBoxAltIdStr:
        case SandBoxIdStr:
            return !TFsPath(s).IsAbsolute();
        case SandBoxPathStr:
            return TFsPath(s).IsAbsolute();
        default:
            break;
    }
    return false;
}

void NToolsCachePrivate::CheckConfig(const NConfig::TConfig& config) {
    using namespace NConfig;

    TDict tcSection;
    try {
        tcSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();
    } catch (const TTypeMismatch&) {
        Cerr << "Missing or malformed [" << IniSectionName() << "] section" << Endl;
        throw;
    }

    for (auto e : {DbPathStr}) {
        if (!tcSection.contains(ToString(e)) || tcSection.At(ToString(e)).Get<TString>().empty()) {
            MissingEntryError(e);
            ythrow TTypeMismatch() << "Missing entry in ini file.";
        }
    }

    // TString + TFsPath
    for (auto e : {LockFileStr, DbPathStr, LogNameStr, SandBoxAltIdStr, SandBoxIdStr, SandBoxPathStr}) {
        try {
            if (tcSection.contains(ToString(e))) {
                auto& s = tcSection.At(ToString(e)).Get<TString>();
                if (!CheckFsPathParameter(s, e)) {
                    ythrow TTypeMismatch() << "'" << ToString(e) << "' '" << s << "' should be absolute path";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // int
    for (auto e : {RunningMaxQueueStr, GcMaxQueueStr, QuiescenceStr, VersionStr, PollStr}) {
        try {
            if (tcSection.contains(ToString(e))) {
                auto s = tcSection.At(ToString(e)).As<int>();
                if (s < 0) {
                    ythrow TTypeMismatch() << "Negative value is not expected";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // i64
    for (auto e : {GcLimitStr}) {
        try {
            if (tcSection.contains(ToString(e))) {
                auto s = tcSection.At(ToString(e)).As<i64>();
                if (s < 0) {
                    ythrow TTypeMismatch() << "Negative value is not expected";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // bool
    for (auto e : {MasterModeStr, NoDbStr}) {
        try {
            if (tcSection.contains(ToString(e))) {
                tcSection.At(ToString(e)).As<bool>();
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }
}

TString NToolsCachePrivate::GetDBDirectory(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& tcSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();

    if (auto file = tcSection.At(ToString(DbPathStr)).Get<TString>(); !file.empty()) {
        if (file.StartsWith("file:")) {
            // TODO: strip other URI-related components.
            file = file.substr(strlen("file:"));
        }
        return TFsPath(file).Dirname();
    }
    return "";
}

void NToolsCachePrivate::PrepareDirs(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& tcSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();

    for (auto e : {LockFileStr, LogNameStr}) {
        if (tcSection.contains(ToString(e))) {
            if (auto file = tcSection.At(ToString(e)).Get<TString>(); !file.empty()) {
                TFsPath(TFsPath(file).Dirname()).MkDirs(0755);
            }
        }
    }
    TString dbDirName(GetDBDirectory(config));
    TFsPath(dbDirName).MkDirs(0755);
}

TString NToolsCachePrivate::GetLockName(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& tcSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();

    if (tcSection.contains(ToString(LockFileStr))) {
        return tcSection.At(ToString(LockFileStr)).Get<TString>();
    }

    return "";
}

// Deprecated
TString NToolsCachePrivate::GetLogName(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& tcSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();

    if (tcSection.contains(ToString(LogNameStr))) {
        return tcSection.At(ToString(LogNameStr)).Get<TString>();
    }

    return "";
}
