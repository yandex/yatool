#include "registry.h"

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_reader.h>
#include <util/generic/ptr.h>

namespace NUniversalFetcher {

    namespace {
        struct TFetcher {
            TUniversalFetcherFetcherCtor Ctor;
            TVector<TString> Schemas;
        };

        auto& Fetchers() {
            static THashMap<TString, TFetcher> f;
            return f;
        }

        auto& ProcessRunner() {
            static TProcessRunnerPtr runner;
            return runner;
        }

    }

    // TFetchersRegistry* TFetchersRegistry::Get() {
    //     return Singleton<TFetchersRegistry>();
    // }

    // void TFetchersRegistry::Register(TUniversalFetcherFetcherCtor ctor, const TString& name, const TVector<TString>& schemas) {
    //     Fetchers()[name] = TFetcher{std::move(ctor), schemas};
    // }

    void RegisterProcessRunner(TProcessRunnerPtr& runner) {
        ProcessRunner() = runner;
    }

    void RegisterFetcher(TUniversalFetcherFetcherCtor ctor, const TString& name, const TVector<TString>& schemas) {
        Fetchers()[name] = TFetcher{std::move(ctor), schemas};
    }

    THolder<TUniversalFetcher> CreateUniversalFetcher(const NJson::TJsonValue& config, TLogBackendPtr logBackend) {
        auto runner = ProcessRunner();
        Y_ENSURE(runner, "Process runner is not registered, check your ya.make for necessary registrar peerdir");
        TUniversalFetcher::TParams params;
        for (auto&& [fetcherName, fetcherConfig] : config["fetchers"].GetMap()) {
            auto* f = Fetchers().FindPtr(fetcherName);
            if (!f) {
                continue;
            }
            auto fetcher = f->Ctor(fetcherConfig, runner);
            TUniversalFetcher::TRetriesConfig retriesConfig;
            if (fetcherConfig.Has("retries")) {
                retriesConfig = TUniversalFetcher::TRetriesConfig::FromJson(fetcherConfig["retries"]);
            }
            for (auto&& schema : f->Schemas) {
                params.Fetchers[schema] = {.Fetcher = fetcher, .Retries = retriesConfig};
            }
        }
        return THolder<TUniversalFetcher>{new TUniversalFetcher(params, std::move(logBackend))};
    }

    class TFuncLogBackend: public TLogBackend {
    public:
        TFuncLogBackend(std::function<void(ELogPriority, const TString&)> func)
            : Func_(std::move(func))
        {
        }

        void ReopenLog() override {
        }

        void WriteData(const TLogRecord& rec) override {
            // TODO use rec.Priority
            Func_(rec.Priority, TString(rec.Data, rec.Len));
        }

    private:
        std::function<void(ELogPriority, const TString&)> Func_;
    };

    THolder<TUniversalFetcher> CreateUniversalFetcher(const TString& configJson, TUniversalFetcherLoggingFunction logFunction) {
        NJson::TJsonValue json;
        try {
            ReadJsonTree(configJson, &json, true);
        } catch (...) {
            ythrow yexception() << "Failed to parse JSON config: " << CurrentExceptionMessage();
        }
        return CreateUniversalFetcher(json, THolder<TLogBackend>{new TFuncLogBackend(std::move(logFunction))});
    }

    THolder<TUniversalFetcher> CreateUniversalFetcher(const TString& configJson) {
        return CreateUniversalFetcher(configJson, [](ELogPriority priority, const TString& msg) {
            Cerr << "LOG :: " << priority << " :: " << msg << '\n';
        });
    }

}

