#pragma once

#include <Python.h>

#include <utility>

namespace NYMake::NPlugins {
    class TScopedPyObjectPtr {
    public:
        TScopedPyObjectPtr(PyObject* ptr = nullptr) noexcept
            : Ptr_(ptr)
        {
        }

        TScopedPyObjectPtr(const TScopedPyObjectPtr& other) noexcept
            : Ptr_(other.Ptr_)
        {
            Py_XINCREF(Ptr_);
        }

        ~TScopedPyObjectPtr() noexcept {
            Py_XDECREF(Ptr_);
        }

        explicit operator bool() const noexcept {
            return Ptr_ != nullptr;
        }

        operator PyObject* () const noexcept {
            return Get();
        }

        PyObject* Get() const noexcept {
            return Ptr_;
        }

        void Reset(PyObject* ptr) noexcept {
            Py_XDECREF(Ptr_);
            Ptr_ = ptr;
        }

        PyObject* Release() noexcept {
            return std::exchange(Ptr_, nullptr);
        }

        static TScopedPyObjectPtr FromBorrowedRef(PyObject* ref) noexcept {
            Py_XINCREF(ref);
            return TScopedPyObjectPtr{ref};
        }

    private:
        PyObject* Ptr_{nullptr};
    };
}
