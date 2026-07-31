#pragma once

#include <util/system/file.h>
#include <util/generic/ptr.h>
#include <util/generic/refcount.h>
#include <util/generic/noncopyable.h>
#include <util/generic/maybe.h>

#include <exception>
#include <asio/thread_pool.hpp>
#include <asio/post.hpp>

class WrappedFile {
public:
    struct TSlotToken: public TAtomicRefCount<TSlotToken> {
        TSlotToken();
        ~TSlotToken();
    };
public:
    WrappedFile() noexcept = delete;
    WrappedFile(TFile file, TIntrusivePtr<TSlotToken> slot) noexcept
        : File_(std::move(file))
        , Slot_(std::move(slot))
    {
    }

    TFile& GetFile() noexcept {
        return File_;
    }
    const TFile& GetFile() const noexcept {
        return File_;
    }

private:
    TFile File_;
    TIntrusivePtr<TSlotToken> Slot_;
};

class Reclaim: TNonCopyable {
public:
    static Reclaim& Instance() {
        static Reclaim instance;
        return instance;
    }

    static void Init(TMaybe<asio::thread_pool::executor_type> executor) noexcept {
        Instance().Executor_ = std::move(executor);
    }

    WrappedFile OpenFile(const TString& path, EOpenMode mode) {
        TIntrusivePtr<WrappedFile::TSlotToken> slot(new WrappedFile::TSlotToken);
        TFile file(path, mode);
        return WrappedFile(std::move(file), std::move(slot));
    }

    void MarkToRemove(WrappedFile file) {
        if (!Executor_) {
            return;
        }
        asio::post(*Executor_, [f = std::move(file)]() mutable {});
    }

private:
    Reclaim() = default;
    ~Reclaim() = default;

    TMaybe<asio::thread_pool::executor_type> Executor_;
};
