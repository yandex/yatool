#pragma once

#include <Python.h>

#include <devtools/ya/cpp/lib/edl/common/export_helpers.h>
#include <devtools/ya/cpp/lib/edl/common/loaders.h>  // CInteger

#include <library/cpp/pybind/ptr.h>

#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

constexpr bool BORROW = true;

namespace NYa::NEdl {
    inline PyObject* CheckNewObject(PyObject* val) {
        Y_ENSURE(val != nullptr);
        return val;
    }

    inline PyObject* BuildSmallInt(long val) {
#if PY_VERSION_HEX >= 0x03000000
    return CheckNewObject(PyLong_FromLong(val));
#else
    return CheckNewObject(PyInt_FromLong(val));
#endif
    }

    inline PyObject* BuildString(TStringBuf str) {
#if PY_VERSION_HEX >= 0x03000000
        PyObject* pyStr = CheckNewObject(PyUnicode_FromStringAndSize(str.data(), str.size()));
        PyUnicode_InternInPlace(&pyStr);
#else
        PyObject* pyStr = CheckNewObject(PyString_FromStringAndSize(str.data(), str.size()));
        PyString_InternInPlace(&pyStr);
#endif
        return pyStr;
    }

    // Forward declarations
    class TPythonExporter;

    template <class T>
    PyObject* ToPyObject(const T&);

    template <class T>
    void ToParentPyObject(TPythonExporter& parent, const T&);

    class TPythonExporter {
    public:
        void ExportNullValue() {
            Object_ = Py_None;
            Py_INCREF(Object_);
        }

        template <class T>
        void ExportValue(const T& val) {
            Object_ = ToPyObject(val);
        }

        template <class T>
        void ExportInnerValue(const T& val) {
            ToParentPyObject(*this, val);
        }

        template <class Iter>
        void ExportRange(Iter b, Iter e) {
            Object_ = CheckNewObject(PyList_New(e - b));
            for (size_t i = 0; b != e; ++b, ++i) {
                PyObject* val = ToPyObject(*b);
                PyList_SET_ITEM(Object_, i, val);
            }
        }

        void OpenMap() {
            Object_ = CheckNewObject(PyDict_New());
        }

        template <class K, class V>
        void AddMapItem(const K& key, const V& val) {
            NPyBind::TPyObjectPtr pyKey{ToPyObject(key), BORROW};
            NPyBind::TPyObjectPtr pyVal{ToPyObject(val), BORROW};
            Y_ENSURE(PyDict_SetItem(Object_, pyKey.Get(), pyVal.Get()) == 0);
        }

        void CloseMap() {
        }

        PyObject* Get() const noexcept {
            Py_XINCREF(Object_);
            return Object_;
        }

        ~TPythonExporter() {
            Py_XDECREF(Object_);
        }

    private:
        PyObject* Object_{nullptr};
    };

    class TPythonInnerExporter {
    public:
        TPythonInnerExporter(TPythonExporter& parent)
            : Parent_{parent}
        {
        }

        void ExportNullValue() {
            // Do nothing if inner object is null
        }

        template <class T>
        void ExportValue(const T& val) {
            ToParentPyObject(Parent_, val);
        }

        void OpenMap() {
        }

        template <class K, class V>
        void AddMapItem(const K& key, const V& val) {
            Parent_.AddMapItem(key, val);
        }

        void CloseMap() {
        }

    private:
        TPythonExporter& Parent_;
    };

    template <class T>
    struct TPythonizer {
        static PyObject* ToPyObject(const T& val) {
            TPythonExporter exp{};
            Export(exp, val);
            return exp.Get();
        }

        static void ToParentPyObject(TPythonExporter& parent, const T& val) {
            TPythonInnerExporter exp{parent};
            Export(exp, val);
        }
    };

    template <class T>
    PyObject* ToPyObject(const T& val) {
        return TPythonizer<T>::ToPyObject(val);
    }

    template <class T>
    void ToParentPyObject(TPythonExporter& parent, const T& val) {
        TPythonizer<T>::ToParentPyObject(parent, val);
    }

    template <>
    struct TPythonizer<bool> {
        static PyObject* ToPyObject(const bool& val) {
            if (val) {
                Py_RETURN_TRUE;
            }
            Py_RETURN_FALSE;
        }
    };


    template <CInteger T>
    requires (std::is_signed_v<T>)
    struct TPythonizer<T> {
        static PyObject* ToPyObject(const T& val) {
            if (val <= Max<long>()) {
                return BuildSmallInt(val);
            } else {
                return CheckNewObject(PyLong_FromLongLong(val));
            }
        }
    };

    template <CInteger T>
    requires (std::is_unsigned_v<T>)
    struct TPythonizer<T> {
        static PyObject* ToPyObject(const T& val) {
            if (val <= static_cast<unsigned long long>(Max<long>())) {
                return BuildSmallInt(val);
            } else {
                return CheckNewObject(PyLong_FromUnsignedLongLong(val));
            }
        }
    };

    template <>
    struct TPythonizer<double> {
        static PyObject* ToPyObject(const double& val) {
            return CheckNewObject(PyFloat_FromDouble(val));
        }
    };

    template <class T>
    requires std::is_convertible_v<T, TStringBuf>
    struct TPythonizer<T> {
        static PyObject* ToPyObject(const T& str) {
            return BuildString(str);
        }
    };
}
