#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <Python.h>

class TBuildConfiguration;

namespace NYMake {
    namespace NPlugins {
        class TPluginMacroImpl: public TMacroImpl, private TNonCopyable {
        private:
            PyObject* Obj_ = nullptr;
            bool NeedPyThreadLock_ = true;

        public:
            void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>* result = nullptr) override;

            TPluginMacroImpl(PyObject*, bool needLock);

            ~TPluginMacroImpl() override;
        };
        void RegisterMacro(TBuildConfiguration& conf, const char* name, PyObject* func);
    }
}
