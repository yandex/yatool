#pragma once

#include <devtools/ymake/plugins/pybridge/raii.h>

#include <Python.h>

class TPluginUnit;
class TBuildConfiguration;

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake();

    void BindYmakeConf(TBuildConfiguration& conf);

    NYMake::NPy::OwnedRef<PyObject> CreateContextObject(TPluginUnit*);
    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName);
}
