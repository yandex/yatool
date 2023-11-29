#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
        class TPluginMacroImpl: public TMacroImpl, private TNonCopyable {
        private:
            PyObject* Obj_;

        public:
            void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>* result = nullptr) override;

            TPluginMacroImpl(PyObject*);

            ~TPluginMacroImpl() override;
        };
        void RegisterMacro(const char* name, PyObject* func);
    }
}
