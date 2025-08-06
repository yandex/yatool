#pragma once

#include <util/generic/noncopyable.h>
#include <util/generic/vector.h>
#include <util/system/yassert.h>

#include <Python.h>

namespace NYMake {
    class TPythonThreadStateScope: public TNonCopyable {
    public:
        TPythonThreadStateScope(PyInterpreterState* interp) {
            if (interp != nullptr) {
                Set(interp);
            }
        }

        ~TPythonThreadStateScope() {
            if (State_ != nullptr) {
                PyThreadState_Clear(State_);
                PyThreadState_DeleteCurrent();
            }
        }

        void Set(PyInterpreterState* interp) {
            Y_ASSERT(State_ == nullptr);
            State_ = PyThreadState_New(interp);
            PyEval_RestoreThread(State_);
        }

    private:
        PyThreadState* State_ = nullptr;
    };

    class TPythonRuntime: public TNonCopyable {
    public:
        void Initialize(size_t count) {
            Y_ASSERT(!Initialized_);

            // Enable UTF-8 mode by default
            PyStatus status;

            PyPreConfig preconfig;
            PyPreConfig_InitPythonConfig(&preconfig);
            preconfig.utf8_mode = 1;
#ifdef MS_WINDOWS
            preconfig.legacy_windows_fs_encoding = 0;
#endif

            status = Py_PreInitialize(&preconfig);
            if (PyStatus_Exception(status)) {
                Py_ExitStatusException(status);
            }

            Py_InitializeEx(0);

            MainState_ = PyThreadState_Get();

            PyInterpreterConfig cfg = {
                .use_main_obmalloc = 0,
                .allow_fork = 0,
                .allow_exec = 0,
                .allow_threads = 1,
                .allow_daemon_threads = 0,
                .check_multi_interp_extensions = 1,
                .gil = PyInterpreterConfig_OWN_GIL,
            };

            Subinterpreters_.resize(count, nullptr);
            for (auto& sub : Subinterpreters_) {
                Py_NewInterpreterFromConfig(&sub, &cfg);
            }
            PyThreadState_Swap(MainState_);

            SavedState_ = PyEval_SaveThread();

            Initialized_ = true;
        }

        void Finalize() {
            Y_ASSERT(Initialized_);

            PyEval_RestoreThread(SavedState_);

            for (auto sub : Subinterpreters_) {
                if (sub) {
                    PyThreadState_Swap(sub);
                    Py_EndInterpreter(sub);
                }
            }

            PyThreadState_Swap(MainState_);

            Py_Finalize();

            Initialized_ = false;
        }

        PyThreadState* GetSubinterpreterState(size_t index) {
            Y_ASSERT(index < Subinterpreters_.size());
            return Subinterpreters_[index];
        }

    private:
        PyThreadState* MainState_ = nullptr;
        PyThreadState* SavedState_ = nullptr;
        TVector<PyThreadState*> Subinterpreters_;
        bool Initialized_ = false;
    };

    class TPythonRuntimeScope: public TPythonRuntime {
    public:
        TPythonRuntimeScope(bool useSubinterpreters, size_t count)
            :  UseSubinterpreters_{useSubinterpreters}
        {
            if (UseSubinterpreters_) {
                Initialize(count);
            }
        }
        ~TPythonRuntimeScope() {
            if (UseSubinterpreters_) {
                Finalize();
            }
        }

        PyInterpreterState* GetSubinterpreterState(size_t index) {
            return UseSubinterpreters_ ? TPythonRuntime::GetSubinterpreterState(index)->interp : nullptr;
        }
    private:
        bool UseSubinterpreters_ = false;
    };
} // namespace NYMake
