#pragma once

#include <Python.h>

namespace NSJson {
    PyObject* LoadFromStream(PyObject* stream, bool internKeys=false, bool internVals=false);
}
