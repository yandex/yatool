#pragma once

#include <util/generic/strbuf.h>

#include <Python.h>

namespace NYMake::NPy {

inline TStringBuf StrContent(PyObject& pystr) noexcept {
    Y_ASSERT(PyUnicode_Check(&pystr));
    Py_ssize_t size = 0;
    const char *data = PyUnicode_AsUTF8AndSize(&pystr, &size);
    return {data, static_cast<size_t>(size)};
}

}
