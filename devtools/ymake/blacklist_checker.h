#pragma once

#include <util/generic/vector.h>

struct TRestoreContext;
struct TTarget;

class TBlacklistChecker {
public:
    TBlacklistChecker() = delete;
    explicit TBlacklistChecker(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets)
        : RestoreContext_(restoreContext)
        , StartTargets_(startTargets)
    {}
    ~TBlacklistChecker() = default;

    bool CheckAll(); // return true if all valid, else return false

private:
    const TRestoreContext& RestoreContext_;
    const TVector<TTarget>& StartTargets_;

    bool HasBlacklist() const;
};
