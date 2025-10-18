#pragma once

#include <Python.h>

class TPluginUnit;

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake();
    PyObject* CreateContextObject(TPluginUnit*);
    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName);
}
