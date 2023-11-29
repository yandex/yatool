#include "proc_info.h"

#if defined(_darwin_)
#include <devtools/executor/proc_info/proc_info_darwin.h>
#elif defined(_linux_)
#include <devtools/executor/proc_info/proc_info_linux.h>
#elif defined(_win_)
#include <devtools/executor/proc_info/proc_info_windows.h>
#endif

#include <util/generic/deque.h>

namespace NProcInfo {
#ifndef _win_
    TParentProcMap GetParentProcMap() {
        TParentProcMap map;
        pid_t ppid;
        for (const auto pid : GetPids()) {
            if (GetParentPid(pid, ppid)) {
                map[pid] = ppid;
            }
        }
        return map;
    }

    TProcMap GetProcMap() {
        TProcMap map;
        pid_t ppid;
        for (const auto pid : GetPids()) {
            if (!map.contains(pid)) {
                map[pid];
            }
            if (GetParentPid(pid, ppid)) {
                map[ppid].push_back(pid);
            }
        }
        return map;
    }
#endif

    void GetChildrenPidsRecursive(pid_t pid, TProcMap& procMap, TVector<pid_t>& result) {
        if (!procMap.contains(pid)) {
            return;
        }
        for (const auto child : procMap[pid]) {
            result.push_back(child);
            GetChildrenPidsRecursive(child, procMap, result);
        }
    }

    TVector<pid_t> GetChildrenPids(pid_t pid, bool recursive) {
        TVector<pid_t> children;
        if (recursive) {
            auto map = GetProcMap();
            GetChildrenPidsRecursive(pid, map, children);
        } else {
            for (const auto [p, pp] : GetParentProcMap()) {
                if (pp == pid) {
                    children.push_back(p);
                }
            }
        }
        return children;
    }
}
