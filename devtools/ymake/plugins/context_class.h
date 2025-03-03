#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        PyObject* CreateContextObject(TPluginUnit*);

        bool ContextTypeInit(PyObject* ymakeModule);
    }
}
