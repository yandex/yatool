#pragma once

#include <devtools/ymake/diag/common_display/display.h>

#include <util/generic/fwd.h>

#include <asio/any_io_executor.hpp>

class TConfMsgManager;

struct TExecContext {
    std::shared_ptr<NCommonDisplay::TLockedStream> LockedStream;
    std::shared_ptr<TConfMsgManager> ConfMsgManager;

    TExecContext(std::shared_ptr<NCommonDisplay::TLockedStream> lockedStream, std::shared_ptr<TConfMsgManager> confManager)
        : LockedStream(std::move(lockedStream))
        , ConfMsgManager(std::move(confManager))
    {}
};

template <class T>
inline thread_local T* CurrentContext = nullptr;

template <class T>
class TExecutorWithContext {
    asio::any_io_executor Underlying_;
    std::shared_ptr<T> Context_;

    struct ContextValueSetter {
        T* Prev;
        ContextValueSetter(T* new_ptr) {
            Prev = CurrentContext<T>;
            CurrentContext<T> = new_ptr;
        }
        ~ContextValueSetter() {
            CurrentContext<T> = Prev;
        }
    };

public:
    TExecutorWithContext(const asio::any_io_executor& underlying, std::shared_ptr<T> context)
        : Underlying_(underlying)
        , Context_(context) {}

    asio::execution_context& query(asio::execution::context_t) const noexcept {
        return Underlying_.query(asio::execution::context);
    }

    static constexpr asio::execution::blocking_t::never_t query(asio::execution::blocking_t) noexcept {
        return asio::execution::blocking.never;
    }

    bool operator==(const TExecutorWithContext& other) const noexcept {
        return Context_.get() == other.Context_.get() && Underlying_ == other.Underlying_;
    }

    bool operator!=(const TExecutorWithContext& other) const noexcept { return !(*this == other); }

    template<typename Func>
    void execute(Func f) const {
        auto ctxCopy = Context_;
        auto wrapper = [ctxCopy, f = std::move(f)]() mutable {
            ContextValueSetter setter(ctxCopy.get());
            f();
        };
        Underlying_.execute(std::move(wrapper));
    }
};
