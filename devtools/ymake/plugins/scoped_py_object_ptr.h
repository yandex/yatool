#pragma once

#include <Python.h>

#include <utility>

namespace NYMake::NPlugins {
    class TScopedPyObjectPtr {
    public:
        TScopedPyObjectPtr(PyObject* ptr = nullptr)
            : Ptr_(ptr)
        {
        }

        TScopedPyObjectPtr(const TScopedPyObjectPtr& other)
            : Ptr_(other.Ptr_)
        {
            Py_XINCREF(Ptr_);
        }

        ~TScopedPyObjectPtr() {
            Py_XDECREF(Ptr_);
        }

        explicit operator bool() const {
            return Ptr_ != nullptr;
        }

        operator PyObject* () const {
            return Get();
        }

        PyObject* Get() const {
            return Ptr_;
        }

        void Reset(PyObject* ptr) {
            Py_XDECREF(Ptr_);
            Ptr_ = ptr;
        }

        PyObject* Release() {
            return std::exchange(Ptr_, nullptr);
        }

    private:
        PyObject* Ptr_{nullptr};
    };
}
