#pragma once

#include <util/generic/noncopyable.h>
#include <util/generic/ptr.h>

#include <Python.h>

namespace NYmake::NPy::NDetail {
struct TDeleter {
    static void Destroy(PyThreadState* obj) noexcept;
};
}

namespace NYmake::NPy {

class TPython: public TNonCopyable {
public:
    TPython() noexcept;
    ~TPython() noexcept;
};

template<typename TPyObj>
using OwnedRef = THolder<TPyObj, NDetail::TDeleter>;

}
