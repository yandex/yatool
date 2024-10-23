#pragma once

class TBuildConfiguration;
struct TMd5Sig;

namespace NConfReader {
    enum class ELoadStatus {
        Success = 0 /* "Success" */,
        DoesNotExist /* "File does not exist" */,
        UnknownFormat /* "Unknown format" */,
        VersionMismatch /* "Version mismatch" */,
        ConfigurationChanged /* "Configuration changed" */,
        UnhandledException /* "Unhandled exception" */,
    };

    enum class ESaveStatus {
        Success = 0 /* "Success" */,
        UnhandledException /* "Unhandled exception" */,
    };

    ELoadStatus LoadCache(TBuildConfiguration& conf, TMd5Sig& confMd5);
    ESaveStatus SaveCache(TBuildConfiguration& conf, const TMd5Sig& confMd5);
}
