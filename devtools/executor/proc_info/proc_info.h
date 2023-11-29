#pragma once

#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/system/compat.h>

namespace NProcInfo {
    using TProcMap = THashMap<pid_t, TVector<pid_t>>;
    using TParentProcMap = THashMap<pid_t, pid_t>;

    bool GetParentPid(const pid_t pid, pid_t& ppid);
    TVector<pid_t> GetPids();
    TProcMap GetProcMap();
    TParentProcMap GetParentProcMap();
    // Returns the pids in pre-order if recursive is true, to make the process killing easier
    TVector<pid_t> GetChildrenPids(const pid_t pid, bool recursive = false);
}
