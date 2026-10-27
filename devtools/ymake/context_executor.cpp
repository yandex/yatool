#include "context_executor.h"

#include <devtools/ymake/diag/mod_stats_manager.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/progress_manager.h>

TExecContext::~TExecContext() {
    ModStatsManager->CheckUnreported();
}

TExecContext::TExecContext(
    std::shared_ptr<NCommonDisplay::TLockedStream> lockedStream,
    std::shared_ptr<TConfMsgManager> confManager,
    std::shared_ptr<TProgressManager> progressManager,
    std::shared_ptr<TModuleStagesStatsManager> modStatsManager,
    std::shared_ptr<TDiagCtrl> diagCtrl,
    std::shared_ptr<IDiagnosticReporter> reporter)
    : LockedStream(std::move(lockedStream))
    , ConfMsgManager(std::move(confManager))
    , DiagnosticReporter(reporter ? std::move(reporter) : ConfMsgManager)
    , ProgressManager(std::move(progressManager))
    , ModStatsManager(std::move(modStatsManager))
    , DiagCtrl(std::move(diagCtrl))
{
}

std::shared_ptr<TExecContext> MakeRenderContext(std::shared_ptr<IDiagnosticReporter> reporter) {
    const auto* parent = CurrentContext<TExecContext>;
    auto stream = parent ? parent->LockedStream
        : std::shared_ptr<NCommonDisplay::TLockedStream>(NCommonDisplay::LockedStream(), [](auto*) {});
    auto context = std::make_shared<TExecContext>(
        std::move(stream), nullptr,
        std::make_shared<TProgressManager>(),
        std::make_shared<TModuleStagesStatsManager>(),
        std::make_shared<TDiagCtrl>(*Diag()), std::move(reporter));
    context->DisplayCutoff = static_cast<unsigned int>(Display()->GetCutoff());
    return context;
}
