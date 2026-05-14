#pragma once

#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/plugins/pybridge/raii.h>
#include <devtools/ymake/plugins/pybridge/ffi_macro.h>

#include <Python.h>

class TBuildConfiguration;

namespace NYMake::NPlugins {
    void RegisterMacro(TBuildConfiguration& conf, const TString& name, NYMake::NPy::OwnedRef<PyObject>&& func);
    void RegisterMacro(TBuildConfiguration& conf, NPy::TFFIMacro&& macro);
}
