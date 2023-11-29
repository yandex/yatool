#include "error.h"
#include "context_class.h"
#include "cmd_context_class.h"
#include "plugin_macro_impl.h"

#include <util/stream/output.h>
#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/algorithm.h>

#include <Python.h>

using namespace NYMake::NPlugins;

extern "C" PyObject* PyInit_pyinit();
void CythonPreinitialize() {
    Y_ABORT_UNLESS(PyImport_AppendInittab("pyinit", PyInit_pyinit) == 0);
}
PyObject* initpyinit() {
    return PyImport_ImportModule("pyinit");
}

extern "C" PyObject* load_plugins(const char* s);

void LoadPlugins(const TVector<TFsPath> &pluginsRoots, TBuildConfiguration *conf) {
    if (pluginsRoots.empty()) {
        return;
    }

    MacroFacade()->Conf = conf;

    CythonPreinitialize();
    Py_Initialize();

    // do not generate *.pyc files in source tree
    PyRun_SimpleString("import sys; sys.dont_write_bytecode = True");

    PyObject* ymakeModule = PyImport_ImportModule("ymake");

    Y_ABORT_UNLESS(ContextTypeInit(ymakeModule));
    Y_ABORT_UNLESS(CmdContextTypeInit(ymakeModule));

    initpyinit();
    // The order of plugin roots does really matter - 'build/plugins' should go first
    for (const auto& pluginsPath : pluginsRoots) {
        if (pluginsPath.Exists()) {
            load_plugins(pluginsPath.c_str());
        }
    }

    CheckForError();

    Py_DecRef(ymakeModule);
}
