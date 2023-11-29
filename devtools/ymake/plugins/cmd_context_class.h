#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        PyObject* CmdContextCall(TPluginUnit* unit, PyObject* argList);

        bool CmdContextTypeInit(PyObject* ymakeModule);
    }
}
