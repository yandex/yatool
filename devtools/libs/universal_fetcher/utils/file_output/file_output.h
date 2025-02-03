#pragma once

#include <atomic>

#include <util/stream/fwd.h>
#include <util/stream/output.h>
#include <util/stream/file.h>
#include <util/generic/store_policy.h>
#include <util/datetime/base.h>

#include <library/cpp/threading/cancellation/cancellation_token.h>

namespace NUniversalFetcher {

    using namespace NThreading;

    class TCancellableFileOutput : public IOutputStream {
    public:
        template <typename... Args>
        inline TCancellableFileOutput(TCancellationToken cancellation, Args&&... args)
            : FileOutput_(std::forward<Args>(args)...)
            , Cancellation_(std::move(cancellation))
        {}

    private:
        void DoWrite(const void* buf, size_t len) override {
            Cancellation_.ThrowIfTokenCancelled();
            FileOutput_.Write(buf, len);
        }

    private:
        TFileOutput FileOutput_;
        TCancellationToken Cancellation_;
    };

}
