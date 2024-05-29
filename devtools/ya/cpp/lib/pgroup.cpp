#include <util/system/env.h>

#ifdef _unix_
    #include <unistd.h>
#endif

namespace NYa {
    bool ShouldSetOwnProcessGroupId(int argc, char** argv) {
        auto ownProcessGroupIdEnv = GetEnv("YA_NEW_PGROUP");
        if (ownProcessGroupIdEnv == "no" || ownProcessGroupIdEnv == "0") {
            return false;
        }

        auto ownProcessGroupIdArg = true;
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] != '-') {
                break;
            }
            if (!strcmp(argv[i], "--no-new-pgroup")) {
                ownProcessGroupIdArg = false;
                break;
            }
        }
        return ownProcessGroupIdArg;
    }

    void SetOwnProcessGroupId(int argc, char** argv) {
#ifdef _unix_
        if (ShouldSetOwnProcessGroupId(argc, argv)) {
            setpgid(0, 0);
            SetEnv("YA_NEW_PGROUP", "no");
        }
#endif
    }
}
