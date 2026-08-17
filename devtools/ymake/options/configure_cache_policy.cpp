#include "configure_cache_policy.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/trace.h>

#include <util/string/builder.h>
#include <util/system/yassert.h>

#include <utility>

namespace {
    void Validate(const TConfigureCacheLoadResult& result) {
        switch (result.Outcome) {
            case EConfigureCacheLoadOutcome::Missing:
                Y_ASSERT(result.Reason == EConfigureCacheUnavailableReason::Missing && !result.DisabledBy);
                return;
            case EConfigureCacheLoadOutcome::Rejected:
                Y_ASSERT(result.Reason && *result.Reason != EConfigureCacheUnavailableReason::Missing);
                Y_ASSERT(!result.DisabledBy);
                return;
            case EConfigureCacheLoadOutcome::Disabled:
                Y_ASSERT(!result.Reason && result.DisabledBy);
                return;
        }
        Y_UNREACHABLE();
    }
}

#ifndef NDEBUG
TConfigureCachePolicy::TDebugAccessGuard::TDebugAccessGuard(std::atomic_flag& accessInProgress) noexcept
    : AccessInProgress_(&accessInProgress)
{
    const bool concurrentAccess = AccessInProgress_->test_and_set(std::memory_order_acquire);
    Y_ASSERT(!concurrentAccess && "TConfigureCachePolicy must not be accessed concurrently");
}

TConfigureCachePolicy::TDebugAccessGuard::TDebugAccessGuard(TDebugAccessGuard&& other) noexcept
    : AccessInProgress_(std::exchange(other.AccessInProgress_, nullptr))
{
}

TConfigureCachePolicy::TDebugAccessGuard::~TDebugAccessGuard() {
    if (AccessInProgress_) {
        AccessInProgress_->clear(std::memory_order_release);
    }
}

TConfigureCachePolicy::TDebugAccessGuard TConfigureCachePolicy::GuardAccess() const noexcept {
    return TDebugAccessGuard{AccessInProgress_};
}

TConfigureCachePolicy::TDebugAccessGuard TConfigureCachePolicy::DebugAcquireAccessForTest() const noexcept {
    return GuardAccess();
}
#endif

TConfigureCacheLoadResult TConfigureCacheLoadResult::Missing(EConfigureCacheKind kind) {
    return {
        kind,
        EConfigureCacheLoadOutcome::Missing,
        EConfigureCacheUnavailableReason::Missing,
        std::nullopt,
    };
}

TConfigureCacheLoadResult TConfigureCacheLoadResult::Rejected(
    EConfigureCacheKind kind,
    EConfigureCacheUnavailableReason reason
) {
    Y_ASSERT(reason != EConfigureCacheUnavailableReason::Missing);
    return {kind, EConfigureCacheLoadOutcome::Rejected, reason, std::nullopt};
}

TConfigureCacheLoadResult TConfigureCacheLoadResult::Disabled(
    EConfigureCacheKind kind,
    EConfigureCacheDisableSource disabledBy
) {
    return {kind, EConfigureCacheLoadOutcome::Disabled, std::nullopt, disabledBy};
}

TConfigureCacheViolation::TConfigureCacheViolation(TConfigureCacheLoadResult result)
    : Result_(std::move(result))
{
    Validate(Result_);
    *this << "configure cache requirement violation: cache=" << ConfigureCacheKindName(Result_.Kind)
          << " outcome=" << ConfigureCacheOutcomeName(Result_.Outcome);
}

size_t TConfigureCachePolicy::Index(EConfigureCacheKind kind) noexcept {
    const auto index = static_cast<size_t>(kind);
    Y_ASSERT(index < CacheCount);
    return index;
}

void TConfigureCachePolicy::SetEnabled(bool enabled) noexcept {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    Enabled_ = enabled;
}

bool TConfigureCachePolicy::IsEnabled() const noexcept {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    return Enabled_;
}

void TConfigureCachePolicy::OnReadFlagMutation(
    EConfigureCacheKind kind,
    bool oldValue,
    bool newValue,
    EConfigureCacheDisableSource source
) {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    auto& disableSource = DisableSources_[Index(kind)];
    if (oldValue && !newValue) {
        disableSource = source;
    } else if (!oldValue && newValue) {
        disableSource.reset();
    }
}

EConfigureCacheDisableSource TConfigureCachePolicy::DisableSource(EConfigureCacheKind kind) const noexcept {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    return DisableSources_[Index(kind)].value_or(EConfigureCacheDisableSource::Default);
}

bool TConfigureCachePolicy::MarkDiagnosticEmitted() noexcept {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    if (DiagnosticEmitted_) {
        return false;
    }
    DiagnosticEmitted_ = true;
    return true;
}

[[noreturn]] void TConfigureCachePolicy::Fail(TConfigureCacheLoadResult result) {
    throw TConfigureCacheViolation(std::move(result));
}

void TConfigureCachePolicy::BeginInternalCacheApplicabilityProbe() {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    Y_ASSERT(!InternalCacheApplicabilityProbeActive_);
    Y_ASSERT(!DeferredInternalCacheFailure_);
    InternalCacheApplicabilityProbeActive_ = true;
}

void TConfigureCachePolicy::OnInternalCacheFailure(TConfigureCacheLoadResult result) {
    Validate(result);
    Y_ASSERT(result.Kind == EConfigureCacheKind::FS || result.Kind == EConfigureCacheKind::Deps);
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    if (InternalCacheApplicabilityProbeActive_) {
        if (!DeferredInternalCacheFailure_) {
            DeferredInternalCacheFailure_ = std::move(result);
        }
        return;
    }
    Fail(std::move(result));
}

void TConfigureCachePolicy::ConfirmInternalCachesApplicable() {
    std::optional<TConfigureCacheLoadResult> failure;
    {
#ifndef NDEBUG
        const auto access = GuardAccess();
#endif
        if (!InternalCacheApplicabilityProbeActive_) {
            return;
        }
        InternalCacheApplicabilityProbeActive_ = false;
        failure = std::move(DeferredInternalCacheFailure_);
        DeferredInternalCacheFailure_.reset();
    }
    if (failure) {
        Fail(std::move(*failure));
    }
}

void TConfigureCachePolicy::ConfirmInternalCachesNotApplicable() noexcept {
#ifndef NDEBUG
    const auto access = GuardAccess();
#endif
    InternalCacheApplicabilityProbeActive_ = false;
    DeferredInternalCacheFailure_.reset();
}

TStringBuf ConfigureCacheKindName(EConfigureCacheKind kind) noexcept {
    switch (kind) {
        case EConfigureCacheKind::FS:
            return "fs"sv;
        case EConfigureCacheKind::Conf:
            return "conf"sv;
        case EConfigureCacheKind::Deps:
            return "deps"sv;
        case EConfigureCacheKind::DM:
            return "dm"sv;
        case EConfigureCacheKind::Count:
            break;
    }
    Y_UNREACHABLE();
}

TStringBuf ConfigureCacheOutcomeName(EConfigureCacheLoadOutcome outcome) noexcept {
    switch (outcome) {
        case EConfigureCacheLoadOutcome::Missing:
            return "missing"sv;
        case EConfigureCacheLoadOutcome::Rejected:
            return "rejected"sv;
        case EConfigureCacheLoadOutcome::Disabled:
            return "disabled"sv;
    }
    Y_UNREACHABLE();
}

TStringBuf ConfigureCacheReasonName(EConfigureCacheUnavailableReason reason) noexcept {
    switch (reason) {
        case EConfigureCacheUnavailableReason::Missing:
            return "missing"sv;
        case EConfigureCacheUnavailableReason::IncompatibleFormat:
            return "incompatible-format"sv;
        case EConfigureCacheUnavailableReason::UpdatedBinary:
            return "updated-binary"sv;
        case EConfigureCacheUnavailableReason::ChangedConfig:
            return "changed-config"sv;
        case EConfigureCacheUnavailableReason::ReadError:
            return "read-error"sv;
        case EConfigureCacheUnavailableReason::Unknown:
            return "unknown"sv;
    }
    Y_UNREACHABLE();
}

TStringBuf ConfigureCacheDisableSourceName(EConfigureCacheDisableSource source) noexcept {
    switch (source) {
        case EConfigureCacheDisableSource::Default:
            return "default"sv;
        case EConfigureCacheDisableSource::CliRebuildGraph:
            return "cli-rebuild-graph"sv;
        case EConfigureCacheDisableSource::CliFsCacheOnly:
            return "cli-fs-cache-only"sv;
        case EConfigureCacheDisableSource::CliCacheConfig:
            return "cli-cache-config"sv;
        case EConfigureCacheDisableSource::RetryCacheConfig:
            return "retry-cache-config"sv;
        case EConfigureCacheDisableSource::ConfCacheDisabled:
            return "conf-cache-disabled"sv;
        case EConfigureCacheDisableSource::DepsControlConf:
            return "deps-control-conf"sv;
    }
    Y_UNREACHABLE();
}

TString FormatConfigureCacheViolation(const TConfigureCacheLoadResult& result) {
    Validate(result);
    if (result.Outcome == EConfigureCacheLoadOutcome::Disabled) {
        return TStringBuilder() << "Error: YMAKE_CONFIGURE_CACHE_CONFLICT cache="
                                << ConfigureCacheKindName(result.Kind)
                                << " disabled-by=" << ConfigureCacheDisableSourceName(*result.DisabledBy);
    }
    Y_ASSERT(result.Outcome == EConfigureCacheLoadOutcome::Missing
             || result.Outcome == EConfigureCacheLoadOutcome::Rejected);
    return TStringBuilder() << "Error: YMAKE_CONFIGURE_CACHE_UNAVAILABLE cache="
                            << ConfigureCacheKindName(result.Kind)
                            << " reason=" << ConfigureCacheReasonName(*result.Reason);
}

void ReportConfigureCacheViolation(TConfigureCachePolicy& policy, const TConfigureCacheViolation& violation) {
    if (!policy.MarkDiagnosticEmitted()) {
        return;
    }

    const auto& result = violation.Result();
    const TString marker = FormatConfigureCacheViolation(result);
    NCommonDisplay::LockedStream()->Emit(TStringBuilder() << marker << '\n');

    NEvent::TConfigureCacheFailure event;
    event.SetCache(ConfigureCacheKindName(result.Kind).data());
    event.SetOutcome(ConfigureCacheOutcomeName(result.Outcome).data());
    if (result.Reason) {
        event.SetReason(ConfigureCacheReasonName(*result.Reason).data());
    }
    if (result.DisabledBy) {
        event.SetDisabledBy(ConfigureCacheDisableSourceName(*result.DisabledBy).data());
    }
    FORCE_TRACE(U, event);
}
