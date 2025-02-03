#include "custom_fetcher.h"

#include <devtools/libs/universal_fetcher/registry/registry.h>

#include <library/cpp/json/json_value.h>

#include <util/generic/yexception.h>
#include <util/string/cast.h>
#include <util/stream/file.h>
#include <util/folder/dirut.h>
#include <util/folder/path.h>

namespace NUniversalFetcher {

    namespace {
        class TCustomFetcher: public IFetcher {
        public:
            TCustomFetcher(const TCustomFetcherParams& params, const TProcessRunnerPtr& runner)
                : Params_(params)
                , ProcessRunner_(runner)
            {}

            TFetchResult Fetch(const TString& uri, const TDstPath& dst, const TFetchParams& params, TCancellationToken cancellation) override {
                Y_UNUSED(params, cancellation); // TODO(jolex007@): fix it

                auto tmpPath = dst.FilePath() + ".tmp";
                TVector<TString> args{Params_.PathToFetcher, uri, tmpPath};

                auto res = ProcessRunner_->Run(args, {}, std::move(cancellation));

                if (res.ExitStatus == 0) {
                    TFsPath(tmpPath).RenameTo(dst.FilePath());
                    return {EFetchStatus::Ok};
                } else {
                    ythrow yexception() << "Custom fetcher failed: " << res.StdErr;
                    return {EFetchStatus::RetriableError, res.StdErr};
                }
            }

            TLog& Log() {
                return *Log_;
            }

            void SetLogger(TLog& log) override final {
                Log_ = &log;
            }

        private:
            TCustomFetcherParams Params_;
            TProcessRunnerPtr ProcessRunner_;
            TLog* Log_ = nullptr;
        };
    }

    TCustomFetcherParams TCustomFetcherParams::FromJson(const NJson::TJsonValue& json) {
        TCustomFetcherParams ret;
        if (json.Has("path_to_fetcher")) {
            ret.PathToFetcher = json["path_to_fetcher"].GetStringSafe();
            Y_ENSURE(ret.PathToFetcher, "Empty 'path_to_fetcher' config param");
        } else {
            ythrow yexception() << "No 'path_to_fetcher' config param";
        }
        return ret;
    }

    TFetcherPtr CreateCustomFetcher(const TCustomFetcherParams& params, const TProcessRunnerPtr& runner) {
        return new TCustomFetcher(params, runner);
    }

}
