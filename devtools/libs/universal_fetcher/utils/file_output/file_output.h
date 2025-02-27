#pragma once

#include <atomic>

#include <util/stream/fwd.h>
#include <util/stream/output.h>
#include <util/stream/file.h>
#include <util/generic/store_policy.h>
#include <util/datetime/base.h>
#include <library/cpp/digest/md5/md5.h>

#include <library/cpp/threading/cancellation/cancellation_token.h>

namespace NUniversalFetcher {

    constexpr const size_t CHECKSUM_LENGTH = 16;

    using namespace NThreading;

    class TCancellableFileOutput : public IOutputStream {
    public:
        template <typename... Args>
        inline TCancellableFileOutput(TCancellationToken cancellation, TAtomicSharedPtr<MD5> Md5, Args&&... args)
            : FileOutput_(std::forward<Args>(args)...)
            , Cancellation_(std::move(cancellation))
            , Md5_(std::move(Md5))
        {
            if (Md5_) {
                Md5_->Init();
            }
        }

    private:
        void DoWrite(const void* buf, size_t len) override {
            Cancellation_.ThrowIfTokenCancelled();
            FileOutput_.Write(buf, len);
            if (Md5_) {
                Md5_->Update(buf, len);
            }
        }

    private:
        TFileOutput FileOutput_;
        TCancellationToken Cancellation_;
        TAtomicSharedPtr<MD5> Md5_;
    };

}
