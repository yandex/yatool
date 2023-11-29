#include "resolve_include.h"
#include <devtools/ymake/common/npath.h>

// fromFile is in internal format (?)
TString ResolveIncludePath(const TStringBuf& path, const TStringBuf& fromFile) {
    TStringBuf src = path;
    Y_ASSERT(src);
    AssertEx(src[0] != NPath::PATH_SEP, "source path is absolute: " << src);

    const char* specPrefixes[] = {"${CURDIR}/", "${ARCADIA_ROOT}/", nullptr};
    bool rel = true;
    for (size_t i = 0; specPrefixes[i] != nullptr; ++i) {
        if (src.StartsWith(specPrefixes[i])) {
            src.Skip(strlen(specPrefixes[i]));
            if (i > 0) {
                rel = false;
            }
        }
    }
    AssertEx(src.find('$') == TStringBuf::npos, "source path contains vars: " << src);

    if (rel) {
        TStringBuf curdir = NPath::Parent(fromFile);
        Y_ASSERT(curdir);
        AssertEx(curdir[0] != NPath::PATH_SEP, "source path current dir is absolute: " << curdir);
        return NPath::SmartJoin(curdir, src);
    }

    TString res =  ArcPath(src);
    if (NPath::NeedFix(res)) {
        res = NPath::Reconstruct(res);
    }
    return res;
}
