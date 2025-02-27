#include "http_fetcher.h"

#include <devtools/libs/universal_fetcher/registry/registry.h>
#include <devtools/libs/universal_fetcher/utils/http/http_request.h>
#include <devtools/libs/universal_fetcher/utils/checksum/checksum.h>
#include <devtools/libs/universal_fetcher/utils/progress/progress.h>
#include <devtools/libs/universal_fetcher/utils/file_output/file_output.h>

#include <library/cpp/string_utils/base64/base64.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/writer/json.h>

#include <util/generic/yexception.h>
#include <util/stream/file.h>
#include <util/folder/dirut.h>
#include <util/folder/path.h>
#include <util/generic/scope.h>
#include <util/generic/guid.h>

namespace NUniversalFetcher {

    namespace {
        class THttpFetcher: public IFetcher {
        public:
            THttpFetcher(const THttpFetcherParams& params, IHttpFetcherMetrics* metrics = nullptr)
                : Params_(params)
                , Metrics_(metrics)
            {
                if (!Metrics_) {
                    Metrics_ = &DummyMetrics_;
                }
            }

            TFetchResult Fetch(const TString& url, const TDstPath& dstPath_, const TFetchParams& params, TCancellationToken cancellation) override {
                TRedirectableHttpClient::TOptions opts;
                opts.SocketTimeout(Params_.SocketTimeout);
                opts.ConnectTimeout(Params_.ConnectTimeout);
                opts.MaxRedirectCount(Params_.MaxRedirectCount);

                auto dstPath = dstPath_;
                // TODO(trofimenkov): Support Content-Disposition: attachment; filename="filename.jpg"
                dstPath.SetFileNameIfEmpty(GetBaseName(url));

                TString requestId = TGUID::Create().AsUuidString();
                THashMap<TString, TString> headers;
                headers["User-Agent"] = Params_.UserAgent;
                headers["X-Request-ID"] = requestId;

                {
                    THolder<TCancellableFileOutput> fileOutput = MakeHolder<TCancellableFileOutput>(cancellation, dstPath.FilePath());
                    THolder<IOutputStream> out = MakeProgressOutputStreamIfNeeded(params.ProgressReporting, 0, std::move(fileOutput));
                    try {
                        DoHttpGetRequest(url, out.Get(), opts, headers, cancellation);
                    } catch (const THttpRequestException& e) {
                        Log() << ELogPriority::TLOG_WARNING << "Failed to fetch resource, status_code=" << e.GetStatusCode() << ", error=" << e.what() << ", request_id=" << requestId;

                        Metrics_->OnFetchCode(e.GetStatusCode());
                        if (e.GetStatusCode() / 100 == 3) {
                            // Too many redirects
                            Metrics_->OnFetchTooManyRedirects();
                        }

                        return {
                            // TODO(trofimenkov): Interpret e.StatusCode
                            .Status = IsRetriableHttpStatus(e.GetStatusCode()) ? EFetchStatus::RetriableError : EFetchStatus::NonRetriableError,
                            .Error = e.what(), // TODO(trofimenkov): don't use e.what()
                            .Attrs = NJson::TJsonMap({
                                {"http_code", e.GetStatusCode()}
                            })
                        };
                    } catch (const THttpException& e) {
                        return {
                            EFetchStatus::RetriableError,
                            e.what()
                        };
                    } catch (const TOperationCancelledException& e) {
                        return {
                            .Status = EFetchStatus::Cancelled,
                            .Error = "Fetch has been cancelled"
                        };
                    } catch (...) {
                        return {EFetchStatus::RetriableError, CurrentExceptionMessage()};
                    }
                }

                Metrics_->OnFetchCode(200);
                Metrics_->OnFetchOk();
                return {
                    .Status = EFetchStatus::Ok,
                    .ResourceInfo{
                        .FileName = dstPath.FileName(),
                        .Size = dstPath.Stat().Size
                    }
                };
            }

            void SetLogger(TLog& log) override final {
                Log_ = &log;
            }

            TLog& Log() {
                return *Log_;
            }
        private:
            bool IsRetriableHttpStatus(int statusCode) {
                if (statusCode / 100 == 5) { // 5xx
                    return true;
                }
                if (statusCode == 0) { // https://a.yandex-team.ru/arcadia/library/cpp/http/simple/http_client.cpp#L273
                    return true;
                }
                if (statusCode == 429) {
                    return true;
                }
                return false;
            }

        private:
            THttpFetcherParams Params_;
            TLog* Log_ = nullptr;
            THolder<IHttpFetcherMetrics> MetricsHolder_;
            IHttpFetcherMetrics* Metrics_ = nullptr;
            static IHttpFetcherMetrics DummyMetrics_;
        };

        IHttpFetcherMetrics THttpFetcher::DummyMetrics_;
    }

    THttpFetcherParams THttpFetcherParams::FromJson(const NJson::TJsonValue& json) {
        THttpFetcherParams ret;
        if (json.Has("user_agent")) {
            ret.UserAgent = json["user_agent"].GetStringSafe();
        }
        if (json.Has("connect_timeout")) {
            ret.ConnectTimeout = FromString<TDuration>(json["connect_timeout"].GetStringSafe());
        }
        if (json.Has("socket_timeout")) {
            ret.SocketTimeout = FromString<TDuration>(json["socket_timeout"].GetStringSafe());
        }
        if (json.Has("max_redirect_count")) {
            ret.MaxRedirectCount = json["max_redirect_count"].GetUIntegerSafe();
        }
        return ret;
    }

    TFetcherPtr CreateHttpFetcher(const THttpFetcherParams& params, IHttpFetcherMetrics* metrics) {
        return new THttpFetcher(params, metrics);
    }

}
