#pragma once

#include "pipeline.h"

#include <util/system/spinlock.h>

#include <memory>

namespace NForeignTargetPipeline {

    // Cancels readers after a peer fails while leaving writers, finalization,
    // and concurrent configuration/rendering work on their original executors.
    class TCancellableForeignTargetPipeline final: public TForeignTargetPipeline {
    public:
        TCancellableForeignTargetPipeline(TForeignTargetPipeline& pipeline, asio::any_io_executor executor);

        THolder<TLineReader> CreateReader(TBuildConfiguration& conf) override;
        THolder<TLineWriter> CreateWriter(TBuildConfiguration& conf) override;
        bool RegisterConfig(const TVector<const char*>& config) override;
        void FinalizeConfig(TBuildConfiguration& conf) override;

        // Also cancels readers created after this call. Join all reader work
        // before destroying the wrapper or the wrapped pipeline.
        void CancelReaders();

    private:
        struct TReaderState;
        class TReader;

        static void CancelReader(const std::shared_ptr<TReaderState>& state);

        TForeignTargetPipeline& Pipeline_;
        asio::any_io_executor Executor_;
        TAdaptiveLock Lock_;
        TVector<std::shared_ptr<TReaderState>> Readers_;
        bool Cancelled_ = false;
    };

} // namespace NForeignTargetPipeline
