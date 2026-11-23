#include "universal_fetcher.h"

#include <library/cpp/logger/null.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/algorithm.h>
#include <util/generic/scope.h>
#include <util/string/builder.h>
#include <util/folder/dirut.h>
#include <util/folder/tempdir.h>
#include <util/folder/iterator.h>
#include <util/system/sysstat.h>

#include <optional>
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
        if (json.Has("sandbox_max_server_error_retry_count")) {
            ret.SandboxMaxServerErrorRetryCount = json["sandbox_max_server_error_retry_count"].GetIntegerSafe();
        }
        return ret;
    }

    namespace {

        // True when the attempt reached the sandbox data transports and each of them answered with HTTP 5xx.
        // Only the sandbox fetcher reports transport history, so this never holds for other fetchers.
        bool FailedWithSandboxServerErrorsOnly(const TFetchResult& result) {
            return !result.TransportHistory.empty() && AllOf(result.TransportHistory, [](const TTransportRequest& req) {
                return req.Attrs.Has("http_code") && req.Attrs["http_code"].GetIntegerRobust() / 100 == 5;
            });
        }

        bool SandboxServerErrorRetriesExhausted(const TVector<TUniversalFetcher::TFetchAttempt>& attempts, const TMaybe<int>& limit) {
            if (!limit.Defined() || std::ssize(attempts) <= *limit) {
                return false;
            }
            return AllOf(attempts, [](const TUniversalFetcher::TFetchAttempt& attempt) {
                return FailedWithSandboxServerErrorsOnly(attempt.Result);
            });
        }

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
            void Execute(Callable&& operation, const TCancellationToken* cancellation) {
                while (RetryCount_ <= Params_.MaxRetryCount) {
                    if (operation() == STOP || (cancellation && RetryCount_ == Params_.MaxRetryCount)) {
                        return;
                    }
                    const auto delay = GetDelay();
                    if (cancellation && !cancellation->IsDefault()) {
                        cancellation->Wait(delay);
                    } else {
                        Sleep(delay);
                    }
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

        const TMaybe<int> sandboxMaxServerErrorRetryCount = params.SandboxMaxServerErrorRetryCount.OrElse(f.Retries.SandboxMaxServerErrorRetryCount);

        const bool cooperative = params.CancellationMode == EFetchCancellationMode::Cooperative;
        TVector<TFetchAttempt> attempts;
        TRetryPolicy policy(f.Retries);
        policy.Execute([&]() {
            attempts.emplace_back();
            auto& attempt = attempts.back();
            attempt.StartTime = TInstant::Now();
            Y_DEFER {
                attempt.Duration = TInstant::Now() - attempt.StartTime;
            };

            if (cooperative && cancellation.IsCancellationRequested()) {
                attempt.Result = {.Status = EFetchStatus::Cancelled, .Error = "Fetch has been cancelled"};
                f.Metrics->OnFetchAttemptFinished(attempt.Result.Status);
                return TRetryPolicy::STOP;
            }

            std::optional<TTempDir> tempDir;
            try {
                path.MkDirs(MODE0777);
                tempDir.emplace(TTempDir::NewTempDir(path.DirPath()));
                // An error removing the temporary files must not escape its destructor.
                tempDir->DoNotRemove();
                TDstPath tempPath = TDstPath::FromDirPath(tempDir->Path());
                Chmod(tempPath.DirPath().c_str(), MODE0777);
                if (path.HasFileName()) {
                    tempPath.SetFileName(path.FileName());
                }

                if (cooperative) {
                    cancellation.ThrowIfCancellationRequested();
                }
                attempt.Result = f.Fetcher->Fetch(uri, tempPath, params, cancellation);
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
            } catch (const NThreading::TOperationCancelledException&) {
                attempt.Result = cooperative
                    ? TFetchResult{.Status = EFetchStatus::Cancelled, .Error = "Fetch has been cancelled"}
                    : TFetchResult{.Status = EFetchStatus::InternalError, .Error = CurrentExceptionMessage()};
            } catch (...) {
                attempt.Result = {
                    .Status = EFetchStatus::InternalError,
                    .Error = CurrentExceptionMessage()
                };
            }

            if (tempDir) {
                try {
                    RemoveDirWithContents(tempDir->Name());
                } catch (...) {
                    attempt.CleanupError = TStringBuilder() << "Failed to remove the temporary fetch directory: " << CurrentExceptionMessage();
                    if (attempt.Result.Status != EFetchStatus::Cancelled) {
                        if (cooperative && cancellation.IsCancellationRequested()) {
                            // A transport can lose its cancellation status if removing
                            // a partial transfer throws after it observes cancellation.
                            TStringBuilder cause;
                            cause << "Fetch has been cancelled";
                            if (attempt.Result.Error) {
                                cause << "; " << *attempt.Result.Error;
                            }
                            attempt.Result.Status = EFetchStatus::Cancelled;
                            attempt.Result.Error = TString{cause};
                        } else {
                            attempt.Result.Status = EFetchStatus::InternalError;
                        }
                    }
                }
            }

            if (attempt.Result.Status == EFetchStatus::RetriableError && SandboxServerErrorRetriesExhausted(attempts, sandboxMaxServerErrorRetryCount)) {
                Log_ << ELogPriority::TLOG_WARNING << "Giving up on " << uri << ": all " << attempts.size() << " attempts failed with HTTP 5xx";
                attempt.Result.Status = EFetchStatus::NonRetriableError;
            }

            f.Metrics->OnFetchAttemptFinished(attempt.Result.Status);
            return attempt.Result.Status == EFetchStatus::RetriableError
                ? TRetryPolicy::CONTINUE
                : TRetryPolicy::STOP;
        }, cooperative ? &cancellation : nullptr);

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

    TString TUniversalFetcher::TFetchAttempt::GetError() const {
        if (!CleanupError) {
            return Result.Error.GetOrElse({});
        }
        TStringBuilder diagnostic;
        if (Result.Error) {
            diagnostic << *Result.Error << "; ";
        } else if (Result.Status == EFetchStatus::Cancelled) {
            diagnostic << "Fetch has been cancelled; ";
        }
        return diagnostic << *CleanupError;
    }

    void TUniversalFetcher::TFetchAttempt::ToJson(NJson::TJsonValue& ret) const {
        ret["start_time_us"] = StartTime.MicroSeconds();
        ret["duration_us"] = Duration.MicroSeconds();
        Result.ToJson(ret["result"]);
        if (CleanupError) {
            // Existing JSON consumers check result.error before publishing the file.
            ret["result"]["error"] = GetError();
        }
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
        return LastAttempt.GetError();
    }

}
