#include "cancellable_pipeline.h"

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>

namespace NForeignTargetPipeline {

    struct TCancellableForeignTargetPipeline::TReaderState {
        explicit TReaderState(asio::any_io_executor executor)
            : Executor(asio::make_strand(std::move(executor)))
        {
        }

        asio::strand<asio::any_io_executor> Executor;
        asio::cancellation_signal Signal;
        bool Cancelled = false;
    };

    class TCancellableForeignTargetPipeline::TReader final: public TLineReader {
    public:
        TReader(THolder<TLineReader> reader, std::shared_ptr<TReaderState> state)
            : Reader_(std::move(reader))
            , State_(std::move(state))
        {
        }

        asio::awaitable<std::optional<TString>> ReadLine() override {
            co_return co_await asio::co_spawn(State_->Executor, ReadOnStrand(), asio::use_awaitable);
        }

    private:
        asio::awaitable<std::optional<TString>> ReadOnStrand() {
            if (State_->Cancelled) {
                throw asio::system_error(asio::error::operation_aborted);
            }
            // Install the slot and emit cancellation on the same strand.
            co_return co_await asio::co_spawn(State_->Executor, Reader_->ReadLine(),
                                              asio::bind_cancellation_slot(State_->Signal.slot(), asio::use_awaitable));
        }

        THolder<TLineReader> Reader_;
        std::shared_ptr<TReaderState> State_;
    };

    TCancellableForeignTargetPipeline::TCancellableForeignTargetPipeline(TForeignTargetPipeline& pipeline, asio::any_io_executor executor)
        : Pipeline_(pipeline)
        , Executor_(std::move(executor))
    {
    }

    THolder<TLineReader> TCancellableForeignTargetPipeline::CreateReader(TBuildConfiguration& conf) {
        auto reader = Pipeline_.CreateReader(conf);
        if (!reader) {
            return nullptr;
        }
        auto state = std::make_shared<TReaderState>(Executor_);
        with_lock (Lock_) {
            Readers_.push_back(state);
            if (Cancelled_) {
                CancelReader(state);
            }
        }
        return MakeHolder<TReader>(std::move(reader), std::move(state));
    }

    THolder<TLineWriter> TCancellableForeignTargetPipeline::CreateWriter(TBuildConfiguration& conf) {
        return Pipeline_.CreateWriter(conf);
    }

    bool TCancellableForeignTargetPipeline::RegisterConfig(const TVector<const char*>& config) {
        return Pipeline_.RegisterConfig(config);
    }

    void TCancellableForeignTargetPipeline::FinalizeConfig(TBuildConfiguration& conf) {
        Pipeline_.FinalizeConfig(conf);
    }

    void TCancellableForeignTargetPipeline::CancelReaders() {
        with_lock (Lock_) {
            Cancelled_ = true;
            for (const auto& reader : Readers_) {
                CancelReader(reader);
            }
        }
    }

    void TCancellableForeignTargetPipeline::CancelReader(const std::shared_ptr<TReaderState>& state) {
        asio::post(state->Executor, [state] {
            state->Cancelled = true;
            state->Signal.emit(asio::cancellation_type::terminal);
        });
    }

} // namespace NForeignTargetPipeline
