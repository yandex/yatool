#include "sysincl_resolver.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>

#include <util/generic/iterator_range.h>

TSysinclResolver::TResult TSysinclResolver::Resolve(TFileView src, TStringBuf include) const {
    include = NPath::ResolveLink(include);
    if (NPath::IsTypedPath(include)) {
        include = NPath::CutType(include);
    }
    YDIAG(Conf) << "Sysincl resolve " << include << " in " << src << Endl;
    Buf = include;
    Buf.to_lower();

    const auto range = MakeIteratorRange(Rules.Headers.equal_range(Buf));
    if (range.begin() == range.end()) {
        return nullptr;
    }

    Result.clear();
    Buf = src.CutType();
    bool match = false;
    for (const auto& [key, rule] : range) {
        if (rule.CaseSensitive && rule.Include != include) {
            continue;
        }
        if (rule.SrcFilter) {
            YDIAG(Conf) << "Matching " << Buf << " against '" << rule.SrcFilter << "'" << Endl;
            const TRegExMatch& filter = Rules.Filters.at(rule.SrcFilter);
            if (!filter.Match(Buf.c_str())) {
                continue;
            }
        }
        match = true;
        Result.insert(Result.end(), rule.Targets.begin(), rule.Targets.end());
    }
    return match ? &Result : nullptr;
}
