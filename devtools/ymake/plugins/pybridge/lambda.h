#pragma once

#include "raii.h"

#include <concepts>
#include <span>

namespace NYMake::NPy::NDetail {

constexpr const char* WRAPPED_CALLABLE_NAME = "WrappedCXXCallable";

template<std::invocable<std::span<PyObject* const>> Callable>
struct TPyLambda: Callable {
    static PyObject* Call(PyObject* self, PyObject* const* args, Py_ssize_t nargs) noexcept {
        if (!PyCapsule_CheckExact(self)) {
            PyErr_SetString(PyExc_TypeError, "Bad call of wrapped C++ callable object: self is expected to be a Capsule with C++ object");
            return nullptr;
        }
        auto realSelf = reinterpret_cast<TPyLambda*>(PyCapsule_GetPointer(self, WRAPPED_CALLABLE_NAME));
        if (!realSelf) {
            PyErr_SetString(PyExc_TypeError, "Bad call of wrapped C++ callable object: self is a Capsule without C++ object");
            return nullptr;
        }

        return (*realSelf)(std::span{args, static_cast<size_t>(nargs)});
    }

    static void Destroy(PyObject* self) {delete reinterpret_cast<TPyLambda*>(PyCapsule_GetPointer(self, WRAPPED_CALLABLE_NAME));}

    static PyMethodDef CallDef;

    template<typename... A>
    TPyLambda(A&&... a): Callable{std::forward<A>(a)...} {}
};
template<std::invocable<std::span<PyObject* const>> Callable>
PyMethodDef TPyLambda<Callable>::CallDef{"cxx_callable_object", (PyCFunction)&Call, METH_FASTCALL, PyDoc_STR("Call wrapped C++ callable object")};

}

namespace NYMake::NPy {

template<typename F>
static OwnedRef<PyObject> MakePyLambda(F&& f) {
    using TLambda = NDetail::TPyLambda<std::remove_cvref_t<F>>;
    auto state = MakeHolder<TLambda>(std::forward<F>(f));
    OwnedRef self{PyCapsule_New(state.get(), NDetail::WRAPPED_CALLABLE_NAME, &TLambda::Destroy)};
    if (!self)
        return {};
    static_cast<void>(state.Release());

    return OwnedRef{PyCFunction_New(&TLambda::CallDef, self.get())};
}

}
