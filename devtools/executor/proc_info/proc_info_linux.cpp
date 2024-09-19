#include "proc_info_linux.h"

#include <util/folder/path.h>
#include <util/stream/file.h>

namespace NProcInfo {
    bool GetParentPid(const pid_t pid, pid_t& ppid) {
        TString path = "/proc/" + ToString(pid) + "/stat";
        TString str;
        try {
            str = TUnbufferedFileInput(path).ReadAll();
        } catch (TFileError) {
            return false;
        }
        if (str.empty()) {
            return false;
        }
        sscanf(str.data(), "%*d %*s %*c %d", &ppid);
        return true;
    }

    TVector<pid_t> GetPids() {
        TFsPath procDir("/proc");
        TVector<pid_t> pids;
        TVector<TString> dirs;

        for (size_t i = 0; i < 10; i++) {
            // ListNames may fail with "No child process" error
            try {
                procDir.ListNames(dirs);
            } catch (TIoSystemError) {
                continue;
            }
            break;
        }

        pids.resize(dirs.size());
        pid_t pid = 0;
        size_t npids = 0;
        for (const auto& dir : dirs) {
            if (TryFromString<pid_t>(dir, pid)) {
                pids[npids++] = pid;
            }
        }
        pids.resize(npids);
        return pids;
    }
}
