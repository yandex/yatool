#pragma once

#include <util/generic/vector.h>

struct TRestoreContext;
struct TTarget;

class TBlacklistChecker {
public:
    TBlacklistChecker() = delete;
    explicit TBlacklistChecker(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets)
        : RestoreContext_(restoreContext)
        , StartTargets_(startTargets)
        , RecurseRestoreContext_(recurseRestoreContext)
        , RecurseStartTargets_(recurseStartTargets)
    {}
    ~TBlacklistChecker() = default;

    bool CheckAll(); // return true if all valid, else return false

private:
    const TRestoreContext& RestoreContext_;
    const TVector<TTarget>& StartTargets_;
    const TRestoreContext& RecurseRestoreContext_;
    const TVector<TTarget>& RecurseStartTargets_;

    bool HasBlacklist() const;
};
