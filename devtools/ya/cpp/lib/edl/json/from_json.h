#pragma once

#include <devtools/ya/cpp/lib/edl/common/loaders.h>

namespace NYa::NEdl {
    void LoadJson(TStringBuf in, TLoaderPtr&& loader);
    void LoadJson(IInputStream& in, TLoaderPtr&& loader);

    template <class T>
    inline void LoadJson(TStringBuf in, T& val) {
        LoadJson(in, GetLoader(val));
    }

    template <class T>
    inline void LoadJson(IInputStream& in, T& val) {
        LoadJson(in, GetLoader(val));
    }
}
