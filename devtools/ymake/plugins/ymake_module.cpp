#include "ymake_module.h"

#include "convert.h"
#include "error.h"
#include "scoped_py_object_ptr.h"
#include "ymake_module_adapter.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/lang/plugin_facade.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
    typedef struct {
        PyObject_HEAD
        TPluginUnit* Unit;
    } Context;

    static PyObject* ContextTypeGetAttrFunc(PyObject* self, char* attrname) {
        Context* context = reinterpret_cast<Context*>(self);
        PyObject* obj = CreateCmdContextObject(context->Unit, attrname);
        CheckForError();
        return obj;
    }

    static PyTypeObject ContextType = {
        .ob_base=PyVarObject_HEAD_INIT(nullptr, 0)
        .tp_name="ymake.Context",
        .tp_basicsize=sizeof(Context),
        .tp_getattr=ContextTypeGetAttrFunc,
        .tp_flags=Py_TPFLAGS_DEFAULT,
        .tp_doc="Context objects",
        .tp_new=PyType_GenericNew,
    };

    typedef struct {
        PyObject_HEAD
        std::string Name;
        TPluginUnit* Unit;
    } CmdContext;

    static PyObject* CmdContextCall(PyObject* self, PyObject* args, PyObject* /*kwargs*/) {
        CmdContext* cmdContext = reinterpret_cast<CmdContext*>(self);

        TVector<TStringBuf> methodArgs;
        Flatten(args, methodArgs);

        if (cmdContext->Name == TStringBuf("set")) {
            Y_ABORT_UNLESS(methodArgs.size() == 2);
            cmdContext->Unit->Set(methodArgs[0], methodArgs[1]);
            Py_IncRef(self);
            return self;
        } else if (cmdContext->Name == TStringBuf("enabled")) {
            Y_ABORT_UNLESS(methodArgs.size() == 1);
            bool contains = cmdContext->Unit->Enabled(methodArgs[0]);
            return NPyBind::BuildPyObject(contains);
        } else if (cmdContext->Name == TStringBuf("get")) {
            Y_ABORT_UNLESS(methodArgs.size() == 1);
            TStringBuf value = cmdContext->Unit->Get(methodArgs[0]);
            return NPyBind::BuildPyObject(value);
        } else if (cmdContext->Name == TStringBuf("name")) {
            return NPyBind::BuildPyObject(cmdContext->Unit->UnitName());
        } else if (cmdContext->Name == TStringBuf("filename")) {
            return NPyBind::BuildPyObject(cmdContext->Unit->UnitFileName());
        } else if (cmdContext->Name == TStringBuf("global_filename")) {
            return NPyBind::BuildPyObject(cmdContext->Unit->GetGlobalFileName());
        } else if (cmdContext->Name == TStringBuf("path")) {
            return NPyBind::BuildPyObject(cmdContext->Unit->UnitPath());
        } else if (cmdContext->Name == TStringBuf("resolve")) { //TODO: rename resolve here to smth else like get_abs_path
            return NPyBind::BuildPyObject(cmdContext->Unit->ResolveToAbsPath(methodArgs[0]));
        } else if (cmdContext->Name == TStringBuf("resolve_arc_path")) {
            return NPyBind::BuildPyObject(cmdContext->Unit->ResolveToArcPath(methodArgs[0]));
        } else if (cmdContext->Name == TStringBuf("resolve_to_bin_dir_localized")) {
            Y_ABORT_UNLESS(methodArgs.size() == 1);
            return NPyBind::BuildPyObject(cmdContext->Unit->ResolveToBinDirLocalized(methodArgs[0]));
        } else if (cmdContext->Name.starts_with("on")) {
            TString macroName = cmdContext->Name.substr(2);
            macroName.to_upper();
            cmdContext->Unit->CallMacro(macroName, methodArgs);
            Py_IncRef(self);
            return self;
        } else if (cmdContext->Name == TStringBuf("resolve_include")) {
            Y_ABORT_UNLESS(methodArgs.size() > 1);
            TVector<TStringBuf> includes(methodArgs.begin() + 1, methodArgs.end());
            TVector<TString> resolved;
            cmdContext->Unit->ResolveInclude(methodArgs[0], includes, resolved);
            return NPyBind::BuildPyObject(resolved);
        } else if (cmdContext->Name == TStringBuf("message")) {
            Y_ABORT_UNLESS(methodArgs.size() == 2);
            TString status(methodArgs[0]);
            status.to_upper();
            if (status == "INFO") {
                YConfInfo(PluginErr) << methodArgs[1] << Endl;
            } else if (status == "WARN") {
                YConfWarn(PluginErr) << methodArgs[1] << Endl;
            } else if (status == "ERROR") {
                YConfErr(PluginErr) << methodArgs[1] << Endl;
            } else {
                YErr() << "Unknown message status in plugin: " << status << Endl;
            }
            Py_IncRef(self);
            return self;
        } else if (cmdContext->Name == TStringBuf("set_property")) {
            Y_ABORT_UNLESS(methodArgs.size() == 2);
            cmdContext->Unit->SetProperty(methodArgs[0], methodArgs[1]);
            Py_IncRef(self);
            return self;
        } else if (cmdContext->Name == "add_dart") {
            Y_ABORT_UNLESS(methodArgs.size() >= 2);
            TVector<TStringBuf> vars(methodArgs.begin() + 2, methodArgs.end());
            cmdContext->Unit->AddDart(methodArgs[0], methodArgs[1], vars);
            Py_IncRef(self);
            return self;
        }
        ythrow yexception() << "Invalid call: " + cmdContext->Name;
        return nullptr;
    }

    static int CmdContextInit(CmdContext* self, PyObject* args, PyObject* kwds) {
        const char* str;

        static char* kwlist[] = {const_cast<char*>("name"), nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &str)) {
            return -1;
        }
        self->Name = str;

        return 0;
    }

    static PyTypeObject CmdContextType = {
        .ob_base=PyVarObject_HEAD_INIT(nullptr, 0)
        .tp_name="ymake.CmdContext",
        .tp_basicsize=sizeof(CmdContext),
        .tp_call=CmdContextCall,
        .tp_flags=Py_TPFLAGS_DEFAULT,
        .tp_doc="CmdContext objects",
        .tp_init=reinterpret_cast<initproc>(CmdContextInit),
        .tp_new=PyType_GenericNew,
    };

    PyObject* MethodAddParser(PyObject* /*self*/, PyObject* args, PyObject* kwargs) {
        const char* ext;
        PyObject* confObj;
        PyObject* callableObj;
        PyObject* inducedDepsObj = NULL;
        int passInducedIncludes = 0;
        const char* keys[] = {
            "",
            "",
            "",
            "induced",
            "pass_induced_includes",
            nullptr
        };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OsO|$Op:ymake.add_parser", (char**)keys, &confObj, &ext, &callableObj, &inducedDepsObj, &passInducedIncludes) || PyErr_Occurred()) {
            return NULL;
        }
        TBuildConfiguration* conf = nullptr;
        if (PyCapsule_CheckExact(confObj)) {
            conf = reinterpret_cast<TBuildConfiguration*>(PyCapsule_GetPointer(confObj, "BuildConfiguration"));
        }
        if (conf == nullptr) {
            PyErr_SetString(PyExc_TypeError, "first argument of ymake.add_parser is expected to be a PyCapsule object containing a pointer to TBuildConfiguration");
            return NULL;
        }
        std::map<TString, TString> inducedDeps;
        if (inducedDepsObj) {
            if (!PyDict_Check(inducedDepsObj)) {
                PyErr_SetString(PyExc_TypeError, "'induced' argument of ymake.add_parser is expected to be of type 'dict'");
                return NULL;
            }

            Py_ssize_t pos = 0;
            PyObject* keyObj = NULL;
            PyObject* valueObj = NULL;
            while (PyDict_Next(inducedDepsObj, &pos, &keyObj, &valueObj)) {
                if (!PyUnicode_Check(keyObj)) {
                    PyErr_SetString(PyExc_TypeError, "key of dict (of 'induced' argument) must be a string");
                    return NULL;
                }
                const char* key = PyUnicode_AsUTF8AndSize(keyObj, NULL);
                if (!key || PyErr_Occurred()) {
                    return NULL;
                }
                if (!PyUnicode_Check(valueObj)) {
                    PyErr_SetString(PyExc_TypeError, "value of dict (of 'induced' argument) must be a string");
                    return NULL;
                }
                const char* value = PyUnicode_AsUTF8AndSize(valueObj, NULL);
                if (!value || PyErr_Occurred()) {
                    return NULL;
                }
                inducedDeps.emplace(key, value);
            }
        }
        AddParser(conf, ext, callableObj, inducedDeps, passInducedIncludes);
        Py_RETURN_NONE;
    }

    PyObject* MethodReportConfigureError(PyObject* /*self*/, PyObject* args, PyObject* kwargs) {
        const char* errorMessage = nullptr;
        const char* missingDir = nullptr;
        const char* keys[] = {
            "",
            "missing_dir",
            nullptr
        };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$s:ymake.report_configure_error", (char**)keys, &errorMessage, &missingDir) || PyErr_Occurred()) {
            return NULL;
        }
        if (missingDir) {
            // It seems that we can get rid of the second argument for report_configure_error,
            // since calls to report_configure_error with two arguments is currently used in tests
            // only.
            TScopedPyObjectPtr formatObj = Py_BuildValue("s", "format");
            TScopedPyObjectPtr errorMessageObj = Py_BuildValue("s", errorMessage);
            TScopedPyObjectPtr missingDirObj = Py_BuildValue("s", missingDir);
            TScopedPyObjectPtr result = PyObject_CallMethodObjArgs(errorMessageObj, formatObj.Get(), missingDirObj.Get(), NULL);
            if (!result || PyErr_Occurred()) {
                return NULL;
            }

            const char* message = PyUnicode_AsUTF8AndSize(result, NULL);
            if (!message || PyErr_Occurred()) {
                return NULL;
            }

            OnBadDirError(message, missingDir);
        } else {
            OnConfigureError(errorMessage);
        }
        Py_RETURN_NONE;
    }

    PyObject* MethodParseCythonIncludes(PyObject* /*self*/, PyObject* args) {
        const char* data;
        if (!PyArg_ParseTuple(args, "y:ymake.parse_cython_includes", &data) || PyErr_Occurred()) {
            return NULL;
        }
        TVector<TString> includes;
        ParseCythonIncludes(data, includes);
        Py_ssize_t size = static_cast<Py_ssize_t>(includes.size());
        TScopedPyObjectPtr list = PyList_New(size);
        for (Py_ssize_t index = 0; index < size; ++index) {
            if (PyList_SetItem(list, index, Py_BuildValue("y", includes[index].data())) < 0 || PyErr_Occurred()) {
                return NULL;
            }
        }
        return list.Release();
    }

    int YMakeExec(PyObject* mod) {
        if (PyType_Ready(&ContextType) < 0) {
            return -1;
        }
        Py_INCREF(reinterpret_cast<PyObject*>(&ContextType));

        PyModule_AddObject(mod, "Context", reinterpret_cast<PyObject*>(&ContextType));

        if (PyType_Ready(&CmdContextType) < 0) {
            return -1;
        }
        Py_INCREF(reinterpret_cast<PyObject*>(&CmdContextType));

        PyModule_AddObject(mod, "CmdContext", reinterpret_cast<PyObject*>(&CmdContextType));

        return 0;
    }

    PyMethodDef YMakeMethods[] = {
        {"add_parser", (PyCFunction)MethodAddParser, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Add a parser for files with the given extension")},
        {"report_configure_error", (PyCFunction)MethodReportConfigureError, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Report configure error")},
        {"parse_cython_includes", MethodParseCythonIncludes, METH_VARARGS, PyDoc_STR("Parse Cython includes")},
        {NULL, NULL, 0, NULL}
    };

    PyModuleDef_Slot YMakeSlots[] = {
        {Py_mod_exec, (void*)YMakeExec},
        {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
        {0, NULL}
    };

    struct PyModuleDef ymakemodule = {
        PyModuleDef_HEAD_INIT,           // m_base
        "ymake",                         // m_name
        PyDoc_STR("Interface to YMake"), // m_doc
        0,                               // m_size
        YMakeMethods,                    // m_methods
        YMakeSlots,                      // m_slots
        NULL,                            // m_traverse
        NULL,                            // m_clear
        NULL,                            // m_free
    };
} // anonymous namespace

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake(void) {
        return PyModuleDef_Init(&ymakemodule);
    }

    PyObject* CreateContextObject(TPluginUnit* unit) {
        TScopedPyObjectPtr args = Py_BuildValue("()");
        TScopedPyObjectPtr ymakeModule = PyImport_ImportModule("ymake");
        CheckForError();
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(&ContextType), args);
        if (obj) {
            Context* context = reinterpret_cast<Context*>(obj);
            context->Unit = unit;
        }
        return obj;
    }

    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName) {
        TScopedPyObjectPtr args = Py_BuildValue("(s)", attrName);
        TScopedPyObjectPtr ymakeModule = PyImport_ImportModule("ymake");
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(&CmdContextType), args);
        if (obj) {
            CmdContext* cmdContext = reinterpret_cast<CmdContext*>(obj);
            cmdContext->Unit = unit;
        }
        return obj;
    }
}
