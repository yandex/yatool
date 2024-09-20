#include "docker_fetcher.h"

#include <devtools/experimental/universal_fetcher/registry/registry.h>

#include <library/cpp/json/json_value.h>

#include <util/generic/yexception.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/stream/file.h>
#include <util/folder/dirut.h>
#include <util/folder/path.h>

namespace NUniversalFetcher {

    namespace {
        class TDockerFetcher: public IFetcher {
        public:
            using TParams = TDockerFetcherConfig;

            TDockerFetcher(const TParams& params, const TProcessRunnerPtr& runner, IDockerFetcherMetrics* metrics = nullptr)
                : Params_(params)
                , ProcessRunner_(runner)
                , Metrics_(metrics)
            {
                if (!Metrics_) {
                    Metrics_ = &DummyMetrics_;
                }
                // Y_ENSURE(params.AuthJsonFile);
                // TODO(trofimenkov): better access(2) check
                if (params.AuthJsonFile) {
                    TFileInput in(params.AuthJsonFile);
                    in.Skip(1);
                }
            }

            TFetchResult Fetch(const TString& uri, const TDstPath& dst_) override {
                auto dstPath = dst_;
                dstPath.SetFileNameIfEmpty("image.tar");

                dstPath.MkDirs(MODE0777);

                auto tmpPath = dstPath.FilePath() + ".tmp"; // TODO(trofimenkov): mktemp
                TVector<TString> args{"skopeo", "--insecure-policy", "copy", uri, TString("docker-archive:") + tmpPath};
                if (Params_.AuthJsonFile) {
                    args.push_back("--src-authfile");
                    args.push_back(Params_.AuthJsonFile);
                }

                auto res = ProcessRunner_->Run(args, Params_.Timeout);

                // TODO(trofimenkov): Where Skopeo will store temporary files with layers?
                // Setup env for it like in https://a.yandex-team.ru/arcadia/devtools/experimental/podman_recipe/run.sh

                if (res.ExitStatus == 0) {
                    TFsPath(tmpPath).RenameTo(dstPath.FilePath());
                    return {
                        .Status = EFetchStatus::Ok,
                        .ResourceInfo{
                            .FileName = dstPath.FileName(),
                            .Size = dstPath.GetFileSize(),
                        }
                    };
                }

                return {
                    .Status = EFetchStatus::RetriableError, 
                    .Error = TStringBuilder() << "Skopeo failed, exit code: " << res.ExitStatus << ", error message: " << res.StdErr,
                    .Attrs = NJson::TJsonMap({
                        {"exit_code", res.ExitStatus},
                    }),
                };
            }

            TLog& Log() {
                return *Log_;
            }

            void SetLogger(TLog& log) override final {
                Log_ = &log;
            }

        private:
            TParams Params_;
            TProcessRunnerPtr ProcessRunner_;
            TLog* Log_ = nullptr;
            IDockerFetcherMetrics* Metrics_ = nullptr;
            static IDockerFetcherMetrics DummyMetrics_;
        };

        IDockerFetcherMetrics TDockerFetcher::DummyMetrics_;
    }

    TDockerFetcherConfig TDockerFetcherConfig::FromJson(const NJson::TJsonValue& json) {
        TDockerFetcherConfig ret;
        if (json.Has("auth_json_file")) {
            ret.AuthJsonFile = json["auth_json_file"].GetStringSafe();
        }
        if (json.Has("timeout")) {
            ret.Timeout = FromString<TDuration>(json["timeout"].GetStringSafe());
        }
        return ret;
    }

    TFetcherPtr CreateDockerFetcher(const TDockerFetcherConfig& params, const TProcessRunnerPtr& runner, IDockerFetcherMetrics* metrics) {
        return new TDockerFetcher(params, runner, metrics);
    }

}
