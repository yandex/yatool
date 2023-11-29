#include "proc_info_windows.h"

#include <util/system/winint.h>

#include <psapi.h>
#include <tlhelp32.h>

namespace NProcInfo {
    bool GetParentPid(const pid_t pid, pid_t& ppid) {
        PROCESSENTRY32 pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32);

        HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }

        if (Process32First(handle, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    ppid = pe.th32ParentProcessID;
                    CloseHandle(handle);
                    return true;
                }
            } while (Process32Next(handle, &pe));
        }
        CloseHandle(handle);
        return false;
    }

    TParentProcMap GetParentProcMap() {
        TParentProcMap map;
        PROCESSENTRY32 pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32);

        HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            return map;
        }

        if (Process32First(handle, &pe)) {
            do {
                map[pe.th32ProcessID] = pe.th32ParentProcessID;
            } while (Process32Next(handle, &pe));
        }
        CloseHandle(handle);
        return map;
    }

    TProcMap GetProcMap() {
        TProcMap map;
        for (const auto [pid, ppid] : GetParentProcMap()) {
            if (!map.contains(pid)) {
                map[pid];
            }
            map[ppid].push_back(pid);
        }
        return map;
    }

    // For more info see https://a.yandex-team.ru/arc/trunk/arcadia/contrib/python/psutil/src/psutil/arch/windows/process_info.c?rev=4250412#L302
    TVector<pid_t> GetPids() {
        TVector<pid_t> pids;
        DWORD* procList = nullptr;
        int procListSize = 0;
        DWORD procListBytes = 0;
        DWORD enumReturnSize = 0;

        do {
            procListSize += 1024;
            free(procList);
            procListBytes = procListSize * sizeof(DWORD);
            procList = (DWORD*)malloc(procListBytes);
            if (procList == nullptr) {
                return pids;
            }
            if (!EnumProcesses(procList, procListBytes, &enumReturnSize)) {
                free(procList);
                return pids;
            }
        } while (enumReturnSize == procListSize * sizeof(DWORD));

        size_t npids = enumReturnSize / sizeof(DWORD);
        pids.resize((size_t)npids);
        for (size_t i = 0; i < npids; i++) {
            pids[i] = procList[i];
        }
        free(procList);
        return pids;
    }
}
