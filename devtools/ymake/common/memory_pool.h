#pragma once

#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>

struct IMemoryPool {
    virtual ~IMemoryPool();

    virtual void* Allocate(size_t len) = 0;

    void* Append(const void* data, size_t len);

    inline TStringBuf Append(const TStringBuf& s) {
        return {(const char*)Append(s.data(), s.size()), s.size()};
    }

    static TAutoPtr<IMemoryPool> Construct();
};
