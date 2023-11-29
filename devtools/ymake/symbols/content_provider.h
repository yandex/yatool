#pragma once

#include <util/generic/string.h>

class IContentProvider {
public:
    virtual ~IContentProvider() = default;
    virtual TStringBuf Content(TStringBuf path) = 0;
};
