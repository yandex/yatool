#pragma once

#include <devtools/ya/cpp/lib/edl/common/loaders.h>

#include <util/folder/path.h>

namespace NYa::NEdl {
    void LoadJson(TStringBuf in, TLoaderPtr&& loader);
    void LoadJson(IInputStream& in, TLoaderPtr&& loader);
    void LoadJsonFromFile(TFsPath fileName, TLoaderPtr&& loader);

    template <class T>
    inline void LoadJson(TStringBuf in, T& val) {
        LoadJson(in, GetLoader(val));
    }

    template <class T>
    inline void LoadJson(IInputStream& in, T& val) {
        LoadJson(in, GetLoader(val));
    }

    template <class T>
    inline void LoadJsonFromFile(TFsPath fileName, T& val) {
        LoadJsonFromFile(fileName, GetLoader(val));
    }

}
