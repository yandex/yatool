#include "memory_pool.h"

#include <util/memory/segmented_string_pool.h>
#include <util/memory/pool.h>
#include <util/generic/ptr.h>
#include <util/generic/deque.h>

namespace {
    struct TFastPool: public IMemoryPool {
        void* Allocate(size_t len) override {
            return P.Allocate(len);
        }

        TMemoryPool P = 32 * 1024;
    };

    struct TDebugPool: public IMemoryPool {
        void* Allocate(size_t len) override {
            M.push_back(::operator new(len + 1));

            return M.back().Get();
        }

        TDeque<TAutoPtr<void>> M;
    };
}

IMemoryPool::~IMemoryPool() {
}

void* IMemoryPool::Append(const void* data, size_t len) {
    auto res = (char*)Allocate(len + 1);

    if (data) {
        memcpy(res, data, len);
    }

    res[len] = 0;

    return res;
}

TAutoPtr<IMemoryPool> IMemoryPool::Construct() {
#if defined(address_sanitizer_enabled) || defined(memory_sanitizer_enabled)
    return new TDebugPool();
#endif

    return new TFastPool();
}
