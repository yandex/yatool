#include "error.h"
#include "plugin_macro_impl.h"
#include "ymake_module.h"

#include <devtools/ymake/plugins/pybridge/raii.h>

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

        if (NYMake::NPy::OwnedRef initFunc{PyObject_GetAttrString(mod, "register_parsers")}; PyCallable_Check(initFunc.get())) {
            NYMake::NPy::OwnedRef confPtr{PyCapsule_New(&conf, BuildConfigurationName, nullptr)};
            PyObject_CallOneArg(initFunc.get(), confPtr.get());
            if (PyErr_Occurred()) {
                PyErr_Print();
                return false;
            }
        }
        return true;
    }

    void RegisterMacrosFromModule(TBuildConfiguration& conf, PyObject* mod) {
        NYMake::NPy::OwnedRef attrs{PyObject_Dir(mod)};
        if (attrs == nullptr) {
            return;
        }

        Py_ssize_t size = PyList_Size(attrs.get());
        for (Py_ssize_t i = 0; i < size; i++) {
            NYMake::NPy::OwnedRef attr{PyList_GetItem(attrs.get(), i)};
            if (!PyUnicode_Check(attr.get())) {
                continue;
            }
            NYMake::NPy::OwnedRef asciiAttrName{PyUnicode_AsASCIIString(attr.get())};
            if (!PyBytes_Check(asciiAttrName.get())) {
                continue;
            }
            TStringBuf attrName = PyBytes_AsString(asciiAttrName.get());
            constexpr TStringBuf pluginMacroPreffix = "on"sv;
            if (attrName.StartsWith(pluginMacroPreffix)) {
                NYMake::NPy::OwnedRef func{PyObject_GetAttr(mod, attr.get())};
                if (!PyFunction_Check(func.get())) {
                    continue;
                }
                auto macroName = ToUpperUTF8(attrName.SubStr(pluginMacroPreffix.size()));
                RegisterMacro(conf, macroName, std::move(func));
            }
        }
    }
}

void LoadPlugins(const TVector<TFsPath> &pluginsRoots, const TVector<TFsPath> &pluginFiles, const TFsPath& pycache, TBuildConfiguration *conf) {
    if (pluginsRoots.empty()) {
        return;
    }

    if (pluginFiles.empty()) {
        return;
    }

    if (pycache.Exists()) {
        PySys_SetObject("dont_write_bytecode", Py_False);
        NYMake::NPy::OwnedRef cachePath{PyUnicode_FromString(pycache.c_str())};
        PySys_SetObject("pycache_prefix", cachePath.Get());
    } else {
        PySys_SetObject("dont_write_bytecode", Py_True);
    }

    NYMake::NPlugins::BindYmakeConf(*conf);

    // The order of plugin roots does really matter - 'build/plugins' should go first
    for (const auto& root : pluginsRoots) {
        if (root.Exists()) {
            NYMake::NPy::OwnedRef pluginsPath{PyUnicode_FromString(root.GetPath().c_str())};
            PyList_Insert(PySys_GetObject("path"), 0, pluginsPath.get());
        }
    }

    for (const auto& file : pluginFiles) {
        auto baseName = file.Basename();
        TString modName = baseName.substr(0, baseName.size() - 3);
        NYMake::NPy::OwnedRef mod{PyImport_ImportModule(modName.data())};
        if (PyErr_Occurred()) {
            PyErr_Print();
            continue;
        }

        if (!RegisterParsersFromModule(*conf, mod.get()))
            continue;
        RegisterMacrosFromModule(*conf, mod.get());
    }

    CheckForError();
}

std::pair<TVector<TFsPath>, TVector<TFsPath>> DiscoverPluginsFromDirRecursively(const TFsPath& path, bool firstLevel) {
    TVector<TFsPath> allFiles;
    TVector<TFsPath> pluginFiles;
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
        allFiles.push_back(file);
        if (firstLevel && !file.Basename().StartsWith('_')) {
            pluginFiles.push_back(file);
        }
    }

    Sort(dirs, [](const auto& lhs, const auto& rhs) { return lhs.Basename() < rhs.Basename(); });
    for (const auto& dir: dirs) {
        auto [pluginsFromDir, pluginFilesFromDir] = DiscoverPluginsFromDirRecursively(dir, /* firstLevel */ false);
        allFiles.insert(allFiles.end(), pluginsFromDir.begin(), pluginsFromDir.end());
        pluginFiles.insert(pluginFiles.end(), pluginFilesFromDir.begin(), pluginFilesFromDir.end());
    }
    return std::pair(allFiles, pluginFiles);
}

std::pair<TVector<TFsPath>, TVector<TFsPath>> DiscoverPlugins(const TVector<TFsPath> &pluginsRoots) {
    TVector<TFsPath> allFiles;
    TVector<TFsPath> pluginFiles;
    for (const auto& pluginsRoot : pluginsRoots) {
        if (!pluginsRoot.Exists()) {
            continue;
        }
        auto [pluginsFromRoot, pluginFilesFromRoot] = DiscoverPluginsFromDirRecursively(pluginsRoot, /* firstLevel */ true);
        allFiles.insert(allFiles.end(), pluginsFromRoot.begin(), pluginsFromRoot.end());
        pluginFiles.insert(pluginFiles.end(), pluginFilesFromRoot.begin(), pluginFilesFromRoot.end());
    }
    return std::pair(allFiles, pluginFiles);
}
