#pragma once

#include <atomic>

#include <util/stream/fwd.h>
#include <util/stream/output.h>
#include <util/generic/store_policy.h>
#include <util/datetime/base.h>

#include <devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h>

namespace NUniversalFetcher {

    class TProgressOutputStream : public IOutputStream {
    public:
        using TProgressCallback = std::function<void(ui64)>;

        inline TProgressOutputStream(const TProgressCallback& callback, const TDuration& minInterval, THolder<IOutputStream> stream)
            : Stream_(std::move(stream))
            , Callback_(callback)
            , MinCallbackInterval_(minInterval)
        {
            LastProgressCallbackCalled_ = TInstant::Now();
            Callback_(0);
        }

        ~TProgressOutputStream() {
            Stream_->Flush();
        }

    private:
        void DoWrite(const void* buf, size_t len) override {
            Stream_->Write(buf, len);
            WrittenBytes_ += len;
            if (TInstant::Now() - LastProgressCallbackCalled_ >= MinCallbackInterval_) {
                LastProgressCallbackCalled_ = TInstant::Now();
                Callback_(WrittenBytes_);
            }
        }

    private:
        THolder<IOutputStream> Stream_;
        TProgressCallback Callback_;
        TDuration MinCallbackInterval_;

        TInstant LastProgressCallbackCalled_ = TInstant::Zero();
        ui64 WrittenBytes_ = 0;
    };

    THolder<IOutputStream> MakeProgressOutputStreamIfNeeded(const TMaybe<TFetchParams::TProgressReporting>& params, ui64 totalSize, THolder<IOutputStream> stream);

}
