#pragma once

#include "dst_path.h"

#include <devtools/experimental/universal_fetcher/utils/checksum/checksum.h>

#include <library/cpp/logger/priority.h>
#include <library/cpp/logger/log.h>
#include <library/cpp/json/json_value.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/datetime/base.h>

namespace NUniversalFetcher {

    using TLogBackendPtr = THolder<::TLogBackend>;

    enum class EFetchStatus {
        Ok /* "ok" */,
        RetriableError /* "retriable_error" */,
        NonRetriableError /* "non_retriable_error" */,
        InternalError /* "internal_error" */,
    };

    struct TTransportRequest {
        TString TransportName;
        TString Endpoint;
        TInstant StartTime;
        TDuration Duration;
        TMaybe<TString> Error;
        NJson::TJsonMap Attrs;

        void ToJson(NJson::TJsonValue&) const;
    };

    struct TResourceInfo {
        TString FileName{};
        bool Executable = false;
        ui64 Size = 0;
        TMaybe<TChecksumInfo> Checksum{};
        // id -- sbx id
        // rights: read/write?
        // type: sandbox resource type
        // state: sandbox resource state
        NJson::TJsonMap Attrs{}; // Sandbox

        void ToJson(NJson::TJsonValue&) const;
    };

    struct TFetchResult {
        EFetchStatus Status = EFetchStatus::Ok;
        TMaybe<TString> Error{};
        TResourceInfo ResourceInfo{}; // TODO(trofimenkov): TMaybe<>
        TVector<TTransportRequest> TransportHistory{};
        NJson::TJsonMap Attrs{};

        void ToJson(NJson::TJsonValue&) const;
    };

    class IFetcher {
    public:
        virtual ~IFetcher() {
        }

        virtual TFetchResult Fetch(const TString& uri, const TDstPath& path) = 0;

        virtual void SetLogger(TLog&) = 0;
    };

    using TFetcherPtr = TAtomicSharedPtr<IFetcher>;

    class IProcessRunner {
    public:
        virtual ~IProcessRunner() {
        }

        struct TResult {
            int ExitStatus = 0;
            TString StdErr;
        };

        virtual TResult Run(const TVector<TString>& cmd, TDuration timeout) = 0;
    };

    using TProcessRunnerPtr = TAtomicSharedPtr<IProcessRunner>;

}
