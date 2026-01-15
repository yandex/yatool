#include "raii.h"

namespace {

void NoopThreadsShutdown() noexcept {
    // when C threads call PyEval_RestoreThread, Python's threading module creates _DummyThread
    // objects to track them. Even after PyThreadState_DeleteCurrent() is called,
    // these tracking objects may persist, and their _tstate_lock cleanup may not work correctly with OWN_GIL subinterpreters.
    PyRun_SimpleString(
        "import sys\n"
        "if 'threading' in sys.modules:\n"
        "    import threading\n"
        "    threading._shutdown = lambda: None\n"
    );
}

}

namespace NYMake::NPy::NDetail {

void TDeleter::Destroy(PyThreadState* obj) noexcept {
    auto* prev = PyThreadState_Swap(obj);
    NoopThreadsShutdown();
    Py_EndInterpreter(obj);
    PyThreadState_Swap(prev);
}

}

namespace NYMake::NPy {

TPython::TPython() noexcept {
    Py_InitializeEx(0);
}

TPython::~TPython() noexcept {
    NoopThreadsShutdown();
    Py_Finalize();
}

}
