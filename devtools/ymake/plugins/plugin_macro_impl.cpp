#include "error.h"
#include "plugin_macro_impl.h"
#include "ymake_module.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/yndex/yndex.h>

#include <library/cpp/pybind/cast.h>

namespace NYMake::NPlugins {
    void TPluginMacroImpl::Execute(TPluginUnit& unit, const TVector<TStringBuf>& params) {
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

    void RegisterMacro(TBuildConfiguration& conf, const TString& name, PyObject* func) {
        if (!PyFunction_Check(func)) {
            Py_ssize_t size = 0;
            auto *pystr = PyType_GetName(Py_TYPE(func));
            const char *data = PyUnicode_AsUTF8AndSize(pystr, &size);
            YErr() << "Attempt to register plugin macro '" << name << "' with implementation of type '" <<  TStringBuf{data, static_cast<size_t>(size)} << "' which is not a function.";
            return;
        }

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
