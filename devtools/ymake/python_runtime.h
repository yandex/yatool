#pragma once

#include <devtools/ymake/plugins/ymake_module.h>
#include <devtools/ymake/plugins/pybridge/raii.h>

#include <util/generic/noncopyable.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/system/guard.h>
#include <util/system/spinlock.h>
#include <util/system/yassert.h>

#include <optional>

#include <Python.h>

namespace NYMake {
    class TPythonThreadStateScope;
    inline thread_local TPythonThreadStateScope* CurrentThreadStateScope = nullptr;
    inline static TAdaptiveLock ThreadStateSetLock;

    class TPythonThreadStateScope: public TNonCopyable {
    public:
        TPythonThreadStateScope(PyInterpreterState* interp) {
            if (interp != nullptr) {
                Set(interp);
            }
            CurrentThreadStateScope = this;
        }

        ~TPythonThreadStateScope() {
            if (State_ != nullptr) {
                auto guard = Guard(ThreadStateSetLock);
                PyThreadState_Clear(State_);
                PyThreadState_DeleteCurrent();
            }
            CurrentThreadStateScope = nullptr;
        }

        void Set(PyInterpreterState* interp) {
            Y_ASSERT(State_ == nullptr);
            auto guard = Guard(ThreadStateSetLock);
            State_ = PyThreadState_New(interp);
            PyEval_RestoreThread(State_);
        }

    private:
        PyThreadState* State_ = nullptr;
    };

    class TPythonRuntime: public TNonCopyable {
    public:
        void Initialize(size_t count) {
            SubinterpretersCount_ = count;
        }

        void Finalize() {
            auto guard = Guard(InitializedLock_);

            if (!Initialized_) {
                return;
            }

            PyEval_RestoreThread(SavedState_);

            Subinterpreters_.clear();

            PyThreadState_Swap(MainState_);

            Python_.reset();
            Initialized_ = false;
        }

        PyInterpreterState* GetSubinterpreterState(size_t index) {
            if (!Initialized_) {
                Initialize_();
            }
            Y_ASSERT(index < Subinterpreters_.size());
            return Subinterpreters_[index]->interp;
        }

        auto GetSubinterpreterStateGetter(size_t index) {
            return [this, index] {
                return GetSubinterpreterState(index);
            };
        }

    private:

        void Initialize_() {
            auto guard = Guard(InitializedLock_);

            if (Initialized_) {
                return;
            }

            PyImport_AppendInittab("ymake", NPlugins::PyInit_ymake);
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

            Python_.emplace();

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

            Subinterpreters_.reserve(SubinterpretersCount_);
            for (size_t i = 0; i < SubinterpretersCount_; ++i) {
                PyThreadState* state = nullptr;
                Py_NewInterpreterFromConfig(&state, &cfg);
                Subinterpreters_.emplace_back(state);
            }
            PyThreadState_Swap(MainState_);

            SavedState_ = PyEval_SaveThread();

            Initialized_ = true;
        }

        std::optional<NYmake::NPy::TPython> Python_;
        PyThreadState* MainState_ = nullptr;
        PyThreadState* SavedState_ = nullptr;
        TVector<NYmake::NPy::OwnedRef<PyThreadState>> Subinterpreters_;
        bool Initialized_ = false;
        size_t SubinterpretersCount_ = 0;
        TAdaptiveLock InitializedLock_;
    };

    class TPythonRuntimeScope: public TPythonRuntime {
    public:
        TPythonRuntimeScope(size_t count) {
            Initialize(count);
        }
        ~TPythonRuntimeScope() {
            Finalize();
        }

        PyInterpreterState* GetSubinterpreterState(size_t index) {
            return TPythonRuntime::GetSubinterpreterState(index);
        }
    };
} // namespace NYMake
