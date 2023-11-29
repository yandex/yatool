#pragma once

#include <Python.h>

#include <devtools/ya/cpp/lib/edl/common/loaders.h>

namespace NYa::NEdl {
    void FromPyObject(PyObject* obj, TLoaderPtr&& loader);

    template <class T>
    void FromPyObject(PyObject* obj, T& val) {
        FromPyObject(obj, GetLoader(val));
    }
}
