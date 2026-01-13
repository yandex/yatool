#pragma once

#include "scoped_py_object_ptr.h"

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

class TBuildConfiguration;

namespace NYMake::NPlugins {
    void RegisterMacro(TBuildConfiguration& conf, const TString& name, TScopedPyObjectPtr&& func);
}
