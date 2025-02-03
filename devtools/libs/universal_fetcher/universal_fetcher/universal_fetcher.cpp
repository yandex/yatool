#include "universal_fetcher.h"

#include <library/cpp/logger/null.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/scope.h>
#include <util/string/builder.h>
#include <util/folder/tempdir.h>
#include <util/folder/iterator.h>
#include <util/system/sysstat.h>

#include <random>

namespace NUniversalFetcher {

    IFetcherMetrics TUniversalFetcher::DummyMetrics_;

    using TRetriesConfig = TUniversalFetcher::TRetriesConfig;

    TRetriesConfig TRetriesConfig::FromJson(const NJson::TJsonValue& json) {
        TRetriesConfig ret;
        if (json.Has("max_retry_count")) {
            ret.MaxRetryCount = json["max_retry_count"].GetIntegerSafe();
        }
        if (json.Has("initial_delay")) {
            ret.InitialDelay = FromString<TDuration>(json["initial_delay"].GetStringSafe());
        }
        if (json.Has("backoff_multiplier")) {
            ret.BackoffMultiplier = json["backoff_multiplier"].GetDoubleSafe();
        }
        if (json.Has("max_delay")) {
            ret.MaxDelay = FromString<TDuration>(json["max_delay"].GetStringSafe());
        }
        if (json.Has("jitter")) {
            ret.Jitter = json["jitter"].GetBooleanSafe();
        }
        if (json.Has("use_fixed_delay")) {
            ret.UseFixedDelay = json["use_fixed_delay"].GetBooleanSafe();
        }
        return ret;
    }

    namespace {

        class TRetryPolicy {
        public:
            enum {
                STOP,
                CONTINUE
            };

            TRetryPolicy(const TRetriesConfig& params)
                : Params_(params)
            {
            }

            template <typename Callable>
            void Execute(Callable&& operation) {
                while (RetryCount_ <= Params_.MaxRetryCount) {
                    if (operation() == STOP) {
                        return;
                    }

                    Sleep(GetDelay());
                    ++RetryCount_;
                }
            }

        private:
            TRetriesConfig Params_;
            int RetryCount_ = 0;

            TDuration GetDelay() const {
                auto delay = Params_.InitialDelay;
                if (!Params_.UseFixedDelay) {
                    delay = Params_.InitialDelay * std::pow(Params_.BackoffMultiplier, RetryCount_);
                    delay = std::min(delay, Params_.MaxDelay);
                }

                if (Params_.Jitter) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dist(0, delay.MilliSeconds() / 2);
                    delay += TDuration::MilliSeconds(dist(gen));
                }

                return delay;
            }
        };

    }

    TUniversalFetcher::TUniversalFetcher(const TParams& params, TLogBackendPtr logBackend)
        : Fetchers_(params.Fetchers)
        , Log_(logBackend ? std::move(logBackend) : TLogBackendPtr(new TNullLogBackend()))
    {
        for (auto& [_, f] : Fetchers_) {
            f.Fetcher->SetLogger(Log_);
            if (!f.Metrics) {
                f.Metrics = &DummyMetrics_;
            }
        }
    }

    TUniversalFetcher::TResultPtr TUniversalFetcher::DownloadToDir(const TString& uri, const TString& dir, const TFetchParams& params, TCancellationToken cancellation) noexcept {
        return Download(uri, TDstPath::FromDirPath(dir), params, std::move(cancellation));
    }

    TUniversalFetcher::TResultPtr TUniversalFetcher::DownloadToFile(const TString& uri, const TString& file, const TFetchParams& params, TCancellationToken cancellation) noexcept {
        return Download(uri, TDstPath::FromFilePath(file), params, std::move(cancellation));
    }

    TUniversalFetcher::TResultPtr TUniversalFetcher::Download(const TString& uri, const TDstPath& path, const TFetchParams& params, TCancellationToken cancellation) noexcept {
        Log_ << ELogPriority::TLOG_INFO << "Download " << uri << " to " << path.MakeRepr();

        auto ret = MakeHolder<TResult>();
        auto schema = TStringBuf(uri).NextTok(':');

        auto fit = Fetchers_.find(schema);
        if (fit == Fetchers_.end()) {
            ret->LastAttempt.Result = TFetchResult{
                .Status = EFetchStatus::NonRetriableError,
                .Error = TStringBuilder() << "No fetcher for " << schema << " schema"
            };
            return ret;
        }
        auto& f = fit->second;

        f.Metrics->OnRequest();
        TInstant startTime = TInstant::Now();

        TVector<TFetchAttempt> attempts;
        TRetryPolicy policy(f.Retries);
        policy.Execute([&]() {
            attempts.emplace_back();
            auto& attempt = attempts.back();
            attempt.StartTime = TInstant::Now();
            Y_DEFER {
                attempt.Duration = TInstant::Now() - attempt.StartTime;
            };

            path.MkDirs(MODE0777);
            TTempDir tempDir = TTempDir::NewTempDir(path.DirPath());
            TDstPath tempPath = TDstPath::FromDirPath(tempDir.Path());
            Chmod(tempPath.DirPath().c_str(), MODE0777);

            if (path.HasFileName()) {
                tempPath.SetFileName(path.FileName());
            }
            if (cancellation.IsCancellationRequested()) {
                attempt.Result = {
                    .Status = EFetchStatus::Cancelled,
                    .Error = "Fetch has been cancelled"
                };
            }
            try {
                attempt.Result = f.Fetcher->Fetch(uri, tempPath, params, cancellation);
            } catch (...) {
                attempt.Result = {
                    .Status = EFetchStatus::InternalError,
                    .Error = CurrentExceptionMessage()
                };
            }
            if (attempt.Result.Status == EFetchStatus::Ok) {
                TVector<TString> contents;
                TFsPath(tempPath.DirPath()).ListNames(contents);

                for (const auto& name : contents) {
                    TString from = tempPath.DirPath() + "/" + name;
                    TString to = path.DirPath() + "/" + name;

                    TFsPath(to).ForceDelete();
                    if (!NFs::Rename(from, to)) {
                        ythrow yexception() << "Failed to move from tmp dir to destination, error=" << LastSystemErrorText();
                    }
                }
            }

            f.Metrics->OnFetchAttemptFinished(attempt.Result.Status);
            return attempt.Result.Status == EFetchStatus::RetriableError
                ? TRetryPolicy::CONTINUE
                : TRetryPolicy::STOP;
        });

        ret->LastAttempt = std::move(attempts.back());
        attempts.pop_back();
        ret->History = std::move(attempts);
        TDuration duration = TInstant::Now() - startTime;

        if (ret->IsSuccess()) {
            f.Metrics->OnRequestOk(duration);
        } else {
            f.Metrics->OnRequestFailed(duration);
        }
        return ret;
    }

    void TUniversalFetcher::TFetchAttempt::ToJson(NJson::TJsonValue& ret) const {
        ret["start_time_us"] = StartTime.MicroSeconds();
        ret["duration_us"] = Duration.MicroSeconds();
        Result.ToJson(ret["result"]);
    }

    NJson::TJsonValue TUniversalFetcher::TResult::ToJson() const {
        NJson::TJsonValue ret;
        LastAttempt.ToJson(ret["last_attempt"]);
        auto& history = ret["history"];
        history = NJson::TJsonArray();
        for (auto& el : History) {
            history.AppendValue({});
            el.ToJson(history.Back());
        }
        return ret;
    }

    TString TUniversalFetcher::TResult::ToJsonString() const {
        return NJson::WriteJson(ToJson());
    }

    bool TUniversalFetcher::TResult::IsSuccess() const {
        return LastAttempt.Result.Status == EFetchStatus::Ok;
    }

    TString TUniversalFetcher::TResult::GetError() const {
        return LastAttempt.Result.Error.GetOrElse({});
    }

}
