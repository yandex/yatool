#include "proc_info_darwin.h"

#include <errno.h>
#include <sys/sysctl.h>

namespace NProcInfo {
    bool GetParentPid(const pid_t pid, pid_t& ppid) {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, pid};
        struct kinfo_proc kp;
        size_t len = sizeof(struct kinfo_proc);

        if (sysctl(mib, 4, &kp, &len, nullptr, 0) == -1) {
            return false;
        }
        if (len == 0) {
            return false;
        }
        ppid = kp.kp_eproc.e_ppid;
        return true;
    }

    // For more info see https://a.yandex-team.ru/arc/trunk/arcadia/contrib/python/psutil/src/psutil/arch/osx/process_info.c?rev=4235773#L35
    TVector<pid_t> GetPids() {
        struct kinfo_proc* procList = nullptr;
        int mib3[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
        size_t size, size2;
        void* ptr;
        int err;
        int lim = 8;

        size_t num_processes = 0;
        while (lim-- > 0) {
            size = 0;
            if (sysctl((int*)mib3, 3, nullptr, &size, nullptr, 0) == -1)
                break;
            size2 = size + (size >> 3);
            if (size2 > size) {
                ptr = malloc(size2);
                if (ptr == nullptr)
                    ptr = malloc(size);
                else
                    size = size2;
            } else {
                ptr = malloc(size);
            }
            if (ptr == nullptr)
                break;

            if (sysctl((int*)mib3, 3, ptr, &size, nullptr, 0) == -1) {
                err = errno;
                free(ptr);
                if (err != ENOMEM)
                    break;
            } else {
                procList = (struct kinfo_proc*)ptr;
                num_processes = size / sizeof(struct kinfo_proc);
                break;
            }
        }

        TVector<pid_t> res;
        if (num_processes > 0) {
            res.resize(num_processes);
            struct kinfo_proc* orig_address = procList;
            for (size_t i = 0; i < num_processes; i++) {
                res[i] = procList->kp_proc.p_pid;
                procList++;
            }
            free(orig_address);
        }
        return res;
    }
}
