#pragma once

#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <span>

struct IMemoryPool {
    using TView = std::span<const std::byte>;

    virtual ~IMemoryPool();

    virtual void* Allocate(size_t len) = 0;

    void* Append(const void* data, size_t len);

    inline TView Append(TView s) {
        return {(TView::value_type*)Append(s.data(), s.size()), s.size()};
    }

    inline TStringBuf Append(TStringBuf s) {
        return {(const char*)Append(s.data(), s.size()), s.size()};
    }

    static TAutoPtr<IMemoryPool> Construct();
};
