#pragma once

#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

struct TCythonDep {
    enum class EKind {
        Include,
        Cdef,
        CimportSimple,
        CimportFrom,
    };

    TString Path;
    EKind Kind;
    TVector<TString> List;

    template <class TStr>
    TCythonDep(TStr path, EKind kind)
        : Path(path)
        , Kind(kind)
    {
    }

    bool operator==(const TCythonDep& other) const {
        return Path == other.Path && Kind == other.Kind && List == other.List;
    }
};

class TCythonIncludesParser {
public:
    void Parse(IContentHolder& file, TVector<TCythonDep>& includes);
};

// For pybuild.py, parses only native includes
void ParseCythonIncludes(const TString& data, TVector<TString>& includes);
