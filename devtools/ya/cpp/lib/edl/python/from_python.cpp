#include "from_python.h"

#include <library/cpp/pybind/ptr.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

#include <cmath>

using NPyBind::TPyObjectPtr;

namespace NYa::NEdl {
    static constexpr bool BORROW = true;

    struct TPythonUtf8String {
        TPyObjectPtr ObjStrRef;
        TStringBuf Str;
    };

    static TMaybe<TPythonUtf8String> GetUtf8String(PyObject* obj) {
#if PY_MAJOR_VERSION == 2
        if (PyString_Check(obj)) {
            const char* data = PyString_AS_STRING(obj);
            Py_ssize_t len = PyString_GET_SIZE(obj);
            return {{TPyObjectPtr(obj), {data, static_cast<std::size_t>(len)}}};
        }
#endif
        if (PyUnicode_Check(obj)) {
#if (PY_VERSION_HEX >= 0x03030000)
            if (PyUnicode_IS_COMPACT_ASCII(obj)) {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(obj, &len);
                return {{TPyObjectPtr(obj), {data, static_cast<std::size_t>(len)}}};
            }
#endif
            TPyObjectPtr utf8Bytes{PyUnicode_AsUTF8String(obj), BORROW};
            if (!utf8Bytes.Get()) {
                throw yexception() << "Cannot convert unicode to utf-8";
            }
#if PY_MAJOR_VERSION == 3
            const char* data = PyBytes_AS_STRING(utf8Bytes.Get());
            Py_ssize_t len = PyBytes_GET_SIZE(utf8Bytes.Get());
#else
            const char* data = PyString_AS_STRING(utf8Bytes.Get());
            Py_ssize_t len = PyString_GET_SIZE(utf8Bytes.Get());
#endif
            return {{std::move(utf8Bytes), {data, static_cast<std::size_t>(len)}}};
        }
        return {};
    }

    // str(obj)
    static TString GetObjectStr(PyObject* obj) {
        TPyObjectPtr objStrRef{PyObject_Str(obj), BORROW};
        if (objStrRef.Get()) {
            TMaybe<TPythonUtf8String> str = GetUtf8String(objStrRef.Get());
            if (str.Defined()) {
                return TString{str->Str};
            }
        }
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
        return "<Unknown object. Cannot represent as a string>";
    }

    void FromPyObject(PyObject* obj, TLoaderPtr&& loader) {
        if (auto str = GetUtf8String(obj); str.Defined()) {
            loader->SetValue(str->Str);
            return;
        }
        if (PyBool_Check(obj)) {
            loader->SetValue(obj == Py_True);
            return;
        }
        if (PyLong_Check(obj)) {
            long long val = PyLong_AsLongLong(obj);
            if (!PyErr_Occurred()) {
                loader->SetValue(val);
                return;
            }
            if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
                PyErr_Clear();
                unsigned long long uval = PyLong_AsUnsignedLongLong(obj);
                if (!PyErr_Occurred()) {
                    loader->SetValue(uval);
                    return;
                }
            }
            PyErr_Clear();
            throw TLoaderError() << "Cannot convert the following value to [unsigned] long long: " << GetObjectStr(obj);
        }
#if PY_MAJOR_VERSION == 2
        if (PyInt_Check(obj)) {
            long long val = PyInt_AS_LONG(obj);
            loader->SetValue(val);
            return;
        }
#endif
        if (PyFloat_Check(obj)) {
            double val = PyFloat_AsDouble(obj);
            if (!PyErr_Occurred()) {
                if (std::isnan(val) || std::isinf(val)) {
                    throw TLoaderError() << "Nan and Inf values are not permitted: " << GetObjectStr(obj);
                }
                loader->SetValue(val);
                return;
            }
            PyErr_Clear();
            throw TLoaderError() << "Cannot convert the following value to double: " << GetObjectStr(obj);
        }
        if (PyDict_Check(obj)) {
            TPyObjectPtr dictRef{obj};
            loader->EnsureMap();

            PyObject *key;
            PyObject *value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(obj, &pos, &key, &value)) {
                auto str = GetUtf8String(key);
                if (str.Empty()) {
                    throw TLoaderError() << "Dict key is not a string: " << GetObjectStr(key);
                }
                FromPyObject(value, loader->AddMapValue(str->Str));
            }
            loader->Finish();
            return;
        }
        if (PyList_Check(obj)) {
            TPyObjectPtr listRef{obj};
            loader->EnsureArray();
            Py_ssize_t listSize = PyList_Size(obj);
            for (Py_ssize_t pos = 0; pos < listSize; ++pos) {
                PyObject* value = PyList_GET_ITEM(obj, pos);
                FromPyObject(value, loader->AddArrayValue());
            }
            loader->Finish();
            return;
        }
        if (PyTuple_Check(obj)) {
            loader->EnsureArray();
            Py_ssize_t tupleSize = PyTuple_Size(obj);
            for (Py_ssize_t pos = 0; pos < tupleSize; ++pos) {
                PyObject* value = PyTuple_GET_ITEM(obj, pos);
                FromPyObject(value, loader->AddArrayValue());
            }
            loader->Finish();
            return;
        }
        if (obj == Py_None) {
            loader->SetValue(nullptr);
            return;
        }
        throw TLoaderError() << "Unsupported object: " << GetObjectStr(obj);
    }
}
