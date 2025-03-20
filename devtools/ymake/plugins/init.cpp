#include "error.h"
#include "context_class.h"
#include "cmd_context_class.h"
#include "plugin_macro_impl.h"
#include "scoped_py_object_ptr.h"
#include "ymake_module.h"

#include "devtools/ymake/diag/trace.h"

#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/algorithm.h>

#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
    const static char BuildConfigurationName[] = "BuildConfiguration";

    void LoadPluginsFromDirRecursively(TBuildConfiguration& conf, const TFsPath path, bool firstLevel) {
        TVector<TFsPath> dirs;
        TVector<TFsPath> files;

        TVector<TFsPath> names;
        path.List(names);
        for (auto& name : names) {
            const TString& baseName = name.Basename();
            if (baseName.empty()) {
                continue;
            }
            if (name.IsDirectory()) {
                if (baseName == "tests"sv) {
                    continue;
                }
                dirs.push_back(std::move(name));
            } else if (name.IsFile()) {
                if (!baseName.EndsWith(".py"sv) || EqualToOneOf(baseName[0], '.', '~', '#')) {
                    continue;
                }
                files.push_back(std::move(name));
            }
        }

        Sort(files, [](const auto& lhs, const auto& rhs) { return lhs.Basename() < rhs.Basename(); });
        for (const auto& file : files) {
            RegisterPluginFilename(conf, file.GetPath().c_str());

            const auto& baseName = file.Basename();
            if (!firstLevel || baseName[0] == '_') {
                continue;
            }

            TString modName = baseName.substr(0, baseName.size() - 3);
            TScopedPyObjectPtr mod = PyImport_ImportModule(modName.data());
            if (PyErr_Occurred()) {
                PyErr_Print();
                continue;
            }

            if (PyObject_HasAttrString(mod, "register_parsers")) {
                if (TScopedPyObjectPtr initFunc = PyObject_GetAttrString(mod, "register_parsers"); PyCallable_Check(initFunc)) {
                    TScopedPyObjectPtr confPtr = PyCapsule_New(&conf, BuildConfigurationName, NULL);
                    PyObject_CallOneArg(initFunc, confPtr);
                    if (PyErr_Occurred()) {
                        PyErr_Print();
                        continue;
                    }
                }
            }

            TScopedPyObjectPtr attrs = PyObject_Dir(mod);
            if (attrs == nullptr) {
                continue;
            }

            Py_ssize_t size = PyList_Size(attrs);
            for (Py_ssize_t i = 0; i < size; i++) {
                TScopedPyObjectPtr attr = PyList_GetItem(attrs, i);
                if (!PyUnicode_Check(attr)) {
                    continue;
                }
                TScopedPyObjectPtr asciiAttrName = PyUnicode_AsASCIIString(attr);
                if (!PyBytes_Check(asciiAttrName)) {
                    continue;
                }
                TStringBuf attrName = PyBytes_AsString(asciiAttrName);
                if (attrName.StartsWith("on"sv)) {
                    TScopedPyObjectPtr func = PyObject_GetAttr(mod, attr);
                    if (!PyFunction_Check(func)) {
                        continue;
                    }
                    auto macroName = ToUpperUTF8(attrName.SubStr(2));
                    RegisterMacro(conf, macroName.c_str(), func);
                }
            }
        }

        Sort(dirs, [](const auto& lhs, const auto& rhs) { return lhs.Basename() < rhs.Basename(); });
        for (const auto& dir: dirs) {
            LoadPluginsFromDirRecursively(conf, dir, /* firstLevel */ false);
        }
    }

    void LoadPluginsFromDir(TBuildConfiguration& conf, const TFsPath& path) {
        TScopedPyObjectPtr sys = PyImport_ImportModule("sys");
        TScopedPyObjectPtr sysPath = PyObject_GetAttrString(sys, "path");
        TScopedPyObjectPtr pluginsPath = PyUnicode_FromString(path.c_str());
        PyList_Insert(sysPath, 0, pluginsPath);

        LoadPluginsFromDirRecursively(conf, path, /* firstLevel */ true);
    }

    PyGILState_STATE py_gstate;
    thread_local int py_lock_depth = 0;
}

TPyThreadLock::TPyThreadLock() noexcept {
    if (py_lock_depth == 0) {
        py_gstate = PyGILState_Ensure();
    }
    py_lock_depth++;
}
TPyThreadLock::~TPyThreadLock() noexcept {
    --py_lock_depth;
    if (py_lock_depth == 0) {
        PyGILState_Release(py_gstate);
    }
}

void LoadPlugins(const TVector<TFsPath> &pluginsRoots, TBuildConfiguration *conf) {
    if (pluginsRoots.empty()) {
        return;
    }

    // Enable UTF-8 mode by default
    PyStatus status;

    PyPreConfig preconfig;
    PyPreConfig_InitPythonConfig(&preconfig);
    // Enable UTF-8 mode for all (DEVTOOLSSUPPORT-46624)
    preconfig.utf8_mode = 1;
#ifdef MS_WINDOWS
    preconfig.legacy_windows_fs_encoding = 0;
#endif

    status = Py_PreInitialize(&preconfig);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }

    Py_Initialize();
    PyEval_SaveThread();

    TPyThreadLock lk;

    PyInit_ymake();

    CheckForError();

    // do not generate *.pyc files in source tree
    PyRun_SimpleString("import sys; sys.dont_write_bytecode = True");

    TScopedPyObjectPtr ymakeModule = PyImport_ImportModule("ymake");

    Y_ABORT_UNLESS(ContextTypeInit(ymakeModule));
    Y_ABORT_UNLESS(CmdContextTypeInit(ymakeModule));

    // The order of plugin roots does really matter - 'build/plugins' should go first
    for (const auto& pluginsPath : pluginsRoots) {
        if (pluginsPath.Exists()) {
            LoadPluginsFromDir(*conf, pluginsPath);
        }
    }

    CheckForError();
}
