#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName);

        bool CmdContextTypeInit(PyObject* ymakeModule);
    }
}
