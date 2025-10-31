#include "error.h"
#include "plugin_macro_impl.h"
#include "scoped_py_object_ptr.h"
#include "ymake_module.h"

#include <util/folder/path.h>
#include <util/generic/algorithm.h>
#include <util/generic/scope.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>


#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
    const static char BuildConfigurationName[] = "BuildConfiguration";

    bool RegisterParsersFromModule(TBuildConfiguration& conf, PyObject* mod) {
        if (!PyObject_HasAttrString(mod, "register_parsers"))
            return true;

        if (TScopedPyObjectPtr initFunc = PyObject_GetAttrString(mod, "register_parsers"); PyCallable_Check(initFunc)) {
            TScopedPyObjectPtr confPtr = PyCapsule_New(&conf, BuildConfigurationName, NULL);
            PyObject_CallOneArg(initFunc, confPtr);
            if (PyErr_Occurred()) {
                PyErr_Print();
                return false;
            }
        }
        return true;
    }

    void RegisterMacrosFromModule(TBuildConfiguration& conf, PyObject* mod) {
        TScopedPyObjectPtr attrs = PyObject_Dir(mod);
        if (attrs == nullptr) {
            return;
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

            if (!RegisterParsersFromModule(conf, mod))
                continue;
            RegisterMacrosFromModule(conf, mod);
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

}

void LoadPlugins(const TVector<TFsPath> &pluginsRoots, const TFsPath& pycache, TBuildConfiguration *conf) {
    if (pluginsRoots.empty()) {
        return;
    }

    if (pycache.Exists()) {
        PySys_SetObject("dont_write_bytecode", Py_False);
        NYMake::NPlugins::TScopedPyObjectPtr cachePath{PyUnicode_FromString(pycache.c_str())};
        PySys_SetObject("pycache_prefix", cachePath.Get());
    } else {
        PySys_SetObject("dont_write_bytecode", Py_True);
    }

    // The order of plugin roots does really matter - 'build/plugins' should go first
    for (const auto& pluginsPath : pluginsRoots) {
        if (pluginsPath.Exists()) {
            LoadPluginsFromDir(*conf, pluginsPath);
        }
    }

    CheckForError();
}
