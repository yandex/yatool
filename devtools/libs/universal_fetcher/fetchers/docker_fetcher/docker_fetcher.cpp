#include "docker_fetcher.h"

#include <devtools/libs/universal_fetcher/registry/registry.h>

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/yexception.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/string/join.h>
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

            TFetchResult Fetch(const TString& uri, const TDstPath& dst_, const TFetchParams& params, TCancellationToken cancellation) override {
                Y_UNUSED(params, cancellation); // TODO(jolex007@): fix it
                auto dstPath = dst_;
                dstPath.SetFileNameIfEmpty("image.tar");

                try {
                    ui64 imageSize = GetImageSize(uri, cancellation);
                    if (imageSize > Params_.ImageSizeLimit) {
                        Metrics_->OnTooLargeImage();
                        return {
                            .Status = EFetchStatus::NonRetriableError,
                            .Error = TStringBuilder() << "Image size (" << imageSize << ") is more than limit " << Params_.ImageSizeLimit
                        };
                    }
                } catch (const yexception& e) {
                    Metrics_->OnFailedToGetImageSize();
                    return {
                        .Status = EFetchStatus::NonRetriableError,
                        .Error = e.what()
                    };
                }

                TVector<TString> args{Params_.SkopeoBinaryPath, "--insecure-policy", "copy", uri, TString("docker-archive:") + dstPath.FilePath()};
                if (Params_.AuthJsonFile) {
                    args.push_back("--src-authfile");
                    args.push_back(Params_.AuthJsonFile);
                }
                if (Params_._SkopeoArgs) {
                    for (auto& arg : Params_._SkopeoArgs) {
                        args.push_back(arg);
                    }
                }

                IProcessRunner::TRunParams runParams = {
                    .Timeout = Params_.Timeout,
                };
                Log() << TLOG_INFO << "Run skopeo: " << JoinSeq(" ", args);
                auto res = ProcessRunner_->Run(args, runParams, cancellation);

                if (res.StdErr && !res.StdErr.empty() && res.StdErr.find("no image found in image index for architecture") != TString::npos) {
                    Log() << TLOG_INFO << "No image found in image index for architecture, trying to fetch it with --override-os linux";

                    args.clear();
                    args = {Params_.SkopeoBinaryPath, "--insecure-policy", "--override-os", "linux", "copy", uri, TString("docker-archive:") + dstPath.FilePath()};
                    if (Params_.AuthJsonFile) {
                        args.push_back("--src-authfile");
                        args.push_back(Params_.AuthJsonFile);
                    }
                    if (Params_._SkopeoArgs) {
                        for (auto& arg : Params_._SkopeoArgs) {
                            args.push_back(arg);
                        }
                    }

                    Log() << TLOG_INFO << "Run skopeo: " << JoinSeq(" ", args);
                    res = ProcessRunner_->Run(args, runParams, std::move(cancellation));
                }

                // TODO(trofimenkov): Where Skopeo will store temporary files with layers?
                // Setup env for it like in https://a.yandex-team.ru/arcadia/library/recipes/podman_recipe/run.sh

                if (res.ExitStatus == 0) {
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

            ui64 GetImageSize(const TString& uri, TCancellationToken cancellation) {
                auto resJson = GetImageInfo(uri, cancellation);

                if (resJson.Has("manifests")) {
                    Log() << TLOG_INFO << "Manifests found, trying to find amd64 digest to get size";
                    auto newUri = ChooseManifest(uri, resJson);
                    Y_ENSURE(newUri);
                    resJson = GetImageInfo(newUri, cancellation);
                }
                return CalcImageSize(resJson);
            }

            TLog& Log() {
                return *Log_;
            }

            void SetLogger(TLog& log) override final {
                Log_ = &log;
            }

        private:
            ui64 CalcImageSize(const NJson::TJsonValue& jsonManifest) {
                ui64 size = 0;
                size += jsonManifest["config"]["size"].GetUIntegerSafe();
                for (auto& layer : jsonManifest["layers"].GetArraySafe()) {
                    size += layer["size"].GetUIntegerSafe();
                }
                return size;
            }

            TString ChooseManifest(const TString uri, const NJson::TJsonValue& jsonManifest) {
                NJson::TJsonValue::TArray manifests = jsonManifest["manifests"].GetArraySafe();
                Y_ENSURE(manifests.size() > 0);
                auto urlWithImageName = TString(TStringBuf(uri).NextTok('@'));
                for (auto& manifest : manifests) {
                    if (manifest["platform"]["architecture"].GetStringSafe() == "amd64") {
                        TString digest = manifest["digest"].GetStringSafe();
                        auto newUri = TStringBuilder() << urlWithImageName << "@" << digest;
                        return newUri;
                    }
                }
                Log() << TLOG_INFO << "No amd64 manifest found, trying to find any digest to get size";
                auto newUri = TStringBuilder() << urlWithImageName << "@" << manifests[0]["digest"].GetStringSafe();
                return newUri;
            }

            NJson::TJsonValue GetImageInfo(const TString& uri, TCancellationToken cancellation) {
                TVector<TString> args{Params_.SkopeoBinaryPath, "inspect", "--raw"};
                if (Params_.AuthJsonFile) {
                    args.push_back("--authfile");
                    args.push_back(Params_.AuthJsonFile);
                }
                if (Params_._SkopeoArgs) {
                    for (auto& arg : Params_._SkopeoArgs) {
                        args.push_back(arg);
                    }
                }
                args.push_back(uri);

                Log() << TLOG_INFO << "Run skopeo: " << JoinSeq(" ", args);
                TStringStream stdOut;
                IProcessRunner::TRunParams runParams = {
                    .Timeout = Params_.Timeout,
                    .OutputStream = &stdOut
                };
                auto res = ProcessRunner_->Run(args, runParams, std::move(cancellation));
                if (res.ExitStatus) {
                    ythrow yexception() << "Failed to run skopeo: " << res.StdErr;
                }

                NJson::TJsonValue resJson;
                if (!NJson::ReadJsonTree(stdOut.Str(), &resJson)) {
                    ythrow yexception() << "Can't parse skopeo inspect output";
                }

                return resJson;
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
        if (json.Has("skopeo_binary")) {
            ret.SkopeoBinaryPath = json["skopeo_binary"].GetStringSafe();
        }
        if (json.Has("auth_json_file")) {
            ret.AuthJsonFile = json["auth_json_file"].GetStringSafe();
        }
        if (json.Has("timeout")) {
            ret.Timeout = FromString<TDuration>(json["timeout"].GetStringSafe());
        }
        if (json.Has("image_size_limit")) {
            ret.ImageSizeLimit = json["image_size_limit"].GetUIntegerSafe();
        }
        if (json.Has("_skopeo_args")) {
            for (auto& arg : json["_skopeo_args"].GetArraySafe()) {
                ret._SkopeoArgs.push_back(arg.GetStringSafe());
            }
        }
        return ret;
    }

    TFetcherPtr CreateDockerFetcher(const TDockerFetcherConfig& params, const TProcessRunnerPtr& runner, IDockerFetcherMetrics* metrics) {
        return new TDockerFetcher(params, runner, metrics);
    }

}
