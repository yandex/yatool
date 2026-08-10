#pragma once

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/yexception.h>
#include <util/system/types.h>

#include <atomic>
#include <array>
#include <optional>

enum class EConfigureCacheKind : ui8 {
    FS,
    Conf,
    Deps,
    DM,
    Count,
};

enum class EConfigureCacheLoadOutcome : ui8 {
    Loaded,
    Missing,
    Rejected,
    Disabled,
    NotApplicable,
};

enum class EConfigureCacheUnavailableReason : ui8 {
    Missing,
    IncompatibleFormat,
    UpdatedBinary,
    ChangedConfig,
    ReadError,
    Unknown,
};

enum class EConfigureCacheDisableSource : ui8 {
    Default,
    CliRebuildGraph,
    CliFsCacheOnly,
    CliCacheConfig,
    RetryCacheConfig,
    ConfCacheEnabled,
    DepsControlConf,
};

struct TConfigureCacheLoadResult {
    EConfigureCacheKind Kind;
    EConfigureCacheLoadOutcome Outcome;
    std::optional<EConfigureCacheUnavailableReason> Reason;
    std::optional<EConfigureCacheDisableSource> DisabledBy;

    static TConfigureCacheLoadResult Loaded(EConfigureCacheKind kind);
    static TConfigureCacheLoadResult Missing(EConfigureCacheKind kind);
    static TConfigureCacheLoadResult Rejected(
        EConfigureCacheKind kind,
        EConfigureCacheUnavailableReason reason
    );
    static TConfigureCacheLoadResult Disabled(
        EConfigureCacheKind kind,
        EConfigureCacheDisableSource disabledBy
    );
    static TConfigureCacheLoadResult NotApplicable(EConfigureCacheKind kind);
};

class TConfigureCacheViolation final : public yexception {
public:
    explicit TConfigureCacheViolation(TConfigureCacheLoadResult result);

    const TConfigureCacheLoadResult& Result() const noexcept {
        return Result_;
    }

private:
    TConfigureCacheLoadResult Result_;
};

class TConfigureCachePolicy {
public:
    // Threading contract:
    //
    // A policy instance is confined to one configuration flow. The flow may
    // resume on different worker threads, but accesses to the policy must never
    // overlap. In particular, do not capture a policy in concurrently running
    // save/render/preload tasks. Pass value snapshots across such boundaries.
    //
    // Debug builds assert this contract on every state access. This is
    // intentionally not a mutex: silently serializing accidental parallel use
    // would hide a broken configuration-flow ownership contract.
    void SetEnabled(bool enabled) noexcept;

    bool IsEnabled() const noexcept;

    void Record(TConfigureCacheLoadResult result);
    std::optional<TConfigureCacheLoadResult> Result(EConfigureCacheKind kind) const;

    void OnReadFlagMutation(
        EConfigureCacheKind kind,
        bool oldValue,
        bool newValue,
        EConfigureCacheDisableSource source
    );
    EConfigureCacheDisableSource DisableSource(EConfigureCacheKind kind) const noexcept;

    [[noreturn]] void FailMissing(EConfigureCacheKind kind);
    [[noreturn]] void FailRejected(EConfigureCacheKind kind, EConfigureCacheUnavailableReason reason);
    [[noreturn]] void FailDisabled(EConfigureCacheKind kind);

    bool MarkDiagnosticEmitted() noexcept;

#ifndef NDEBUG
    class TDebugAccessGuard {
    public:
        TDebugAccessGuard(const TDebugAccessGuard&) = delete;
        TDebugAccessGuard& operator=(const TDebugAccessGuard&) = delete;
        TDebugAccessGuard(TDebugAccessGuard&& other) noexcept;
        TDebugAccessGuard& operator=(TDebugAccessGuard&&) = delete;
        ~TDebugAccessGuard();

    private:
        friend class TConfigureCachePolicy;
        explicit TDebugAccessGuard(std::atomic_flag& accessInProgress) noexcept;

        std::atomic_flag* AccessInProgress_;
    };

    // Exposed only in debug builds to make the non-concurrent access assertion
    // deterministic in unit tests.
    [[nodiscard]] TDebugAccessGuard DebugAcquireAccessForTest() const noexcept;
#endif

private:
    static size_t Index(EConfigureCacheKind kind) noexcept;
    [[noreturn]] void Fail(TConfigureCacheLoadResult result);

#ifndef NDEBUG
    [[nodiscard]] TDebugAccessGuard GuardAccess() const noexcept;
#endif

private:
    static constexpr size_t CacheCount = static_cast<size_t>(EConfigureCacheKind::Count);

    bool Enabled_ = false;
    bool DiagnosticEmitted_ = false;
    std::array<std::optional<TConfigureCacheLoadResult>, CacheCount> Results_;
    std::array<std::optional<EConfigureCacheDisableSource>, CacheCount> DisableSources_;
#ifndef NDEBUG
    mutable std::atomic_flag AccessInProgress_ = ATOMIC_FLAG_INIT;
#endif
};

TStringBuf ConfigureCacheKindName(EConfigureCacheKind kind) noexcept;
TStringBuf ConfigureCacheOutcomeName(EConfigureCacheLoadOutcome outcome) noexcept;
TStringBuf ConfigureCacheReasonName(EConfigureCacheUnavailableReason reason) noexcept;
TStringBuf ConfigureCacheDisableSourceName(EConfigureCacheDisableSource source) noexcept;
TString FormatConfigureCacheViolation(const TConfigureCacheLoadResult& result);
void ReportConfigureCacheViolation(TConfigureCachePolicy& policy, const TConfigureCacheViolation& violation);
