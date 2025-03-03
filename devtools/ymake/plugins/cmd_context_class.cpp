#include "cmd_context_class.h"
#include "convert.h"

#include <devtools/ymake/diag/manager.h>

#include <library/cpp/pybind/cast.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/cast.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
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

        PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName) {
            PyObject* args = Py_BuildValue("(s)", attrName);
            PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(&CmdContextType), args);
            if (obj) {
                CmdContext* cmdContext = reinterpret_cast<CmdContext*>(obj);
                cmdContext->Unit = unit;
            }
            Py_DECREF(args);
            return obj;
        }

        bool CmdContextTypeInit(PyObject* ymakeModule) {
            if (PyType_Ready(&CmdContextType) < 0) {
                return false;
            }
            Py_INCREF(reinterpret_cast<PyObject*>(&CmdContextType));

            PyModule_AddObject(ymakeModule, "CmdContext", reinterpret_cast<PyObject*>(&CmdContextType));
            return true;
        }
    }
}
