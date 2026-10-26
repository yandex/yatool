#pragma once

#include <Python.h>
#include <util/generic/string.h>

namespace NSJson {
    // Copy a root object, or an object under rootKey, replacing its immediate
    // fields with Python values serialized directly to output. No document tree
    // or intermediate serialized replacement buffers are constructed.
    // rootKey must be bytes or None; replacements must be a dict with bytes keys.
    // Invalid argument types raise Python TypeError before any input/output I/O.
    PyObject* RewriteObjectFromStream(PyObject* input, PyObject* output, PyObject* rootKey, PyObject* replacements);
    PyObject* RewriteObjectFromFile(const TString& path, PyObject* output, PyObject* rootKey, PyObject* replacements);
}
