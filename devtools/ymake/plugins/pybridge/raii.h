#pragma once

#include <util/generic/noncopyable.h>
#include <util/generic/ptr.h>

#include <Python.h>

namespace NYMake::NPy::NDetail {
struct TDeleter {
    static void Destroy(PyThreadState* obj) noexcept;

    static void Destroy(PyTypeObject* obj) noexcept {
        Py_DECREF(obj);
    }
};
}

namespace NYMake::NPy {

class TPython: public TNonCopyable {
public:
    TPython() noexcept;
    ~TPython() noexcept;
};

template<typename TPyObj>
using OwnedRef = THolder<TPyObj, NDetail::TDeleter>;

}
