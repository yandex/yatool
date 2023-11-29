#pragma once

#include <util/generic/vector.h>
#include <util/generic/strbuf.h>

#include <library/cpp/pybind/cast.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        template <class T>
        void Flatten(PyObject* obj, TVector<T>& result) {
            if (obj == Py_None) {
                // 'None' is ok
                return;
            }
            if (NPyBind::ExtractArgs(obj)) {
                // empty args is ok
                return;
            }
            if (!NPyBind::FromPyObject(obj, result) && !NPyBind::ExtractArgs(obj, result)) {
                T var;
                Y_ABORT_UNLESS(NPyBind::ExtractArgs(obj, var));
                result.push_back(var);
            }
        }

        template <typename K, typename V>
        void Flatten(PyObject* obj, TMap<K, V>& result) {
            if (obj == Py_None) {
                // 'None' is ok
                return;
            }
            if (NPyBind::ExtractArgs(obj)) {
                // empty args is ok
                return;
            }
            Y_ABORT_UNLESS(NPyBind::FromPyObject(obj, result));
        }
    }
}
