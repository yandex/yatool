#pragma once

#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>

struct IPathMatcher: public TThrRefBase {
    using TRef = TIntrusivePtr<IPathMatcher>;

    // assume zero-terminated TStringBuf, yep...
    virtual bool Match(TStringBuf data) const = 0;

    static TRef Construct(const TString& regex);
};
