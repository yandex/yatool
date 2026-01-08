#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

class TBuildConfiguration;

namespace NYMake {
    namespace NPlugins {
        class TPluginMacroImpl: public TMacroImpl, private TNonCopyable {
        private:
            PyObject* Obj_ = nullptr;

        public:
            void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params) override;

            TPluginMacroImpl(PyObject*);

            ~TPluginMacroImpl() override;
        };
        void RegisterMacro(TBuildConfiguration& conf, const TString& name, PyObject* func);
    }
}
