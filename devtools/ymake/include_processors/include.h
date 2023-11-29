#pragma once

#include <util/generic/string.h>
#include <util/generic/strbuf.h>

struct TInclude {
    enum class EKind {
        Local,
        System,
        Macro
    };

    TInclude() = default;
    TInclude(const TInclude&) = default;
    TInclude(TInclude&&) = default;

    TInclude(EKind kind, TStringBuf path)
        : Kind(kind)
        , Path(path)
    {
    }

    TInclude(EKind kind, const TString& path)
        : Kind(kind)
        , Path(path)
    {
    }

    TInclude(TStringBuf path)
        : Kind(EKind::Local)
        , Path(path)
    {
    }

    TInclude(const TString& path)
        : Kind(EKind::Local)
        , Path(path)
    {
    }

    inline bool operator<(const TInclude& r) const {
        return Kind < r.Kind || (Kind == r.Kind && Path < r.Path);
    }

    EKind Kind;
    TString Path;
};
