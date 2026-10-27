#pragma once

#include <devtools/ymake/diag/common_display/display.h>

#include <util/generic/fwd.h>

#include <asio/any_io_executor.hpp>

#include <type_traits>

class TConfMsgManager;
class IDiagnosticReporter;
class TProgressManager;
class TModuleStagesStatsManager;
struct TDiagCtrl;
struct TRenderPathContext;

struct TExecContext: TNonCopyable {
    std::shared_ptr<NCommonDisplay::TLockedStream> LockedStream;
    std::shared_ptr<TConfMsgManager> ConfMsgManager;
    std::shared_ptr<IDiagnosticReporter> DiagnosticReporter;
    std::shared_ptr<TProgressManager> ProgressManager;
    std::shared_ptr<TModuleStagesStatsManager> ModStatsManager;
    std::shared_ptr<TDiagCtrl> DiagCtrl;
    unsigned int DisplayCutoff = 4;

    TExecContext(
        std::shared_ptr<NCommonDisplay::TLockedStream> lockedStream,
        std::shared_ptr<TConfMsgManager> confManager,
        std::shared_ptr<TProgressManager> progressManager,
        std::shared_ptr<TModuleStagesStatsManager> modStatsManager,
        std::shared_ptr<TDiagCtrl> diagCtrl,
        std::shared_ptr<IDiagnosticReporter> reporter = {}
    );

    ~TExecContext();
};

template <class T>
inline thread_local T* CurrentContext = nullptr;

template <class T>
class TCurrentContextScope: TNonCopyable {
public:
    explicit TCurrentContextScope(T* context)
        : Previous_(CurrentContext<T>)
    {
        CurrentContext<T> = context;
    }

    ~TCurrentContextScope() {
        CurrentContext<T> = Previous_;
    }

private:
    T* Previous_;
};

template <class T>
class TExecutorWithContext {
    asio::any_io_executor Underlying_;
    std::shared_ptr<T> Context_;

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
            TCurrentContextScope<T> setter(ctxCopy.get());
            if constexpr (std::is_same_v<T, TExecContext>) {
                TCurrentContextScope<TRenderPathContext> renderScope(nullptr);
                f();
            } else {
                f();
            }
        };
        Underlying_.execute(std::move(wrapper));
    }
};

std::shared_ptr<TExecContext> MakeRenderContext(std::shared_ptr<IDiagnosticReporter> reporter);
