#pragma once

#include <Python.h>

class TPluginUnit;
class TBuildConfiguration;

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake();

    void BindYmakeConf(TBuildConfiguration& conf);

    PyObject* CreateContextObject(TPluginUnit*);
    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName);
}
