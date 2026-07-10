#include "error.h"
#include "plugin_macro_impl.h"
#include "ymake_module.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/plugins/pybridge/raii.h>
#include <devtools/ymake/yndex/yndex.h>

#include <library/cpp/pybind/cast.h>

namespace {

    class TPluginFFIMacro: public TMacroImpl, private TNonCopyable {
    public:
        TPluginFFIMacro(const TFsPath& sourceRoot, NYMake::NPy::TFFIMacro&& macro)
            : Macro_{std::move(macro)}
        {
            FillDefinition(sourceRoot);
        }

        void Execute(TPluginUnit& unit, const TVector<TStringBuf>& args) override {
            auto pyUnit = NYMake::NPlugins::CreateContextObject(&unit);
            Macro_.Call(*pyUnit, args);
        }
        const TSignature* Signature() const override { return &Macro_.Signature(); }

        TStringBuf Name() const noexcept {
            return Macro_.Name();
        }

    private:
        void FillDefinition(const TFsPath& sourceRoot) {
            PyCodeObject* code = (PyCodeObject*) PyFunction_GetCode(Macro_.Impl());
            TFsPath path = TFsPath(PyUnicode_AsUTF8(code->co_filename));
            Definition = {
                Macro_.DocText(),
                path.RelativePath(sourceRoot),
                (size_t)code->co_firstlineno,
                1,
                (size_t)code->co_firstlineno,
                1
            };
        }

    private:
        NYMake::NPy::TFFIMacro Macro_;
    };

}

namespace NYMake::NPlugins {

    void RegisterMacro(TBuildConfiguration& conf, NPy::TFFIMacro&& macro) {
        auto plugin = MakeSimpleShared<TPluginFFIMacro>(conf.SourceRoot, std::move(macro));
        conf.RegisterPluginMacro(TString{plugin->Name()}, plugin);
    }
}
