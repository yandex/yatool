#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        PyObject* ContextCall(TPluginUnit*, PyObject* argList);

        bool ContextTypeInit(PyObject* ymakeModule);
    }
}
