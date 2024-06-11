#include <util/system/env.h>

#ifdef _win_ // isatty
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace NYa {
    bool ShouldSetOwnProcessGroupId(int argc, char** argv) {
        auto ownProcessGroupIdEnv = GetEnv("YA_NEW_PGROUP");
        if (ownProcessGroupIdEnv == "no" || ownProcessGroupIdEnv == "0") {
            return false;
        }
        if (ownProcessGroupIdEnv == "yes" || ownProcessGroupIdEnv == "1") {
            return true;
        }

        // Don't set own pgid if user has an interactive session (probably terminal)
        auto isTty = isatty(0) || isatty(1) || isatty(2);
        if (isTty) {
            return false;
        }

        auto setPgid = true;
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] != '-') {
                break;
            }
            if (!strcmp(argv[i], "--no-new-pgroup")) {
                setPgid = false;
                break;
            }
        }
        return setPgid;
    }

    int SetOwnProcessGroupId(int argc, char** argv) {
#ifdef _unix_
        if (ShouldSetOwnProcessGroupId(argc, argv)) {
            setpgid(0, 0);
            SetEnv("YA_NEW_PGROUP", "no");
            return getpid();
        }
#endif
        return 0;
    }
}
