#include "error.h"
#include "context_class.h"
#include "plugin_macro_impl.h"
#include "convert.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/yndex/yndex.h>

#include <library/cpp/pybind/cast.h>

namespace NYMake {
    namespace NPlugins {
        void TPluginMacroImpl::Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>*) {
            PyObject* tupleArgs = PyTuple_New(params.size() + 1);

            PyTuple_SetItem(tupleArgs, 0, CreateContextObject(&unit));
            CheckForError();

            for (size_t i = 0; i < params.size(); ++i) {
                PyTuple_SetItem(tupleArgs, i + 1, PyUnicode_FromString(TString{params[i]}.data()));
            }

            PyObject_CallObject(Obj_, tupleArgs);
            CheckForError();
            Py_DecRef(tupleArgs);
        }

        TPluginMacroImpl::TPluginMacroImpl(PyObject* obj)
            : Obj_(obj)
        {
            Py_XINCREF(Obj_);
        }

        TPluginMacroImpl::~TPluginMacroImpl() {
            Py_XDECREF(Obj_);
        }

        void RegisterMacro(TBuildConfiguration& conf, const char* name, PyObject* func) {
            if (PyFunction_Check(func)) {
                PyCodeObject* code = (PyCodeObject*) PyFunction_GetCode(func);
                TFsPath path = TFsPath(PyUnicode_AsUTF8(code->co_filename));

                TString docText;
                PyObject* doc = ((PyFunctionObject*)func)->func_doc;
                if (PyUnicode_Check(doc)) {
                    docText = PyUnicode_AsUTF8(doc);
                }

                auto macro = MakeSimpleShared<TPluginMacroImpl>(func);
                macro->Definition = {
                    std::move(docText),
                    path.RelativePath(conf.SourceRoot),
                    (size_t)code->co_firstlineno,
                    1,
                    (size_t)code->co_firstlineno,
                    1
                };
                conf.RegisterPluginMacro(name, macro);
            }
        }
    }
}
