#include "error.h"
#include "context_class.h"
#include "cmd_context_class.h"

#include <devtools/ymake/lang/plugin_facade.h>

#include <library/cpp/pybind/cast.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/cast.h>

#include <Python.h>

namespace NYMake {
    namespace NPlugins {
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

        bool ContextTypeInit(PyObject* ymakeModule) {
            if (PyType_Ready(&ContextType) < 0) {
                return false;
            }
            Py_INCREF(reinterpret_cast<PyObject*>(&ContextType));

            PyModule_AddObject(ymakeModule, "Context", reinterpret_cast<PyObject*>(&ContextType));
            return true;
        }

        PyObject* ContextCall(TPluginUnit* unit, PyObject* argList) {
            PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(&ContextType), argList);
            Context* context = reinterpret_cast<Context*>(obj);
            context->Unit = unit;
            return obj;
        }

        PyObject* CreateContextObject(TPluginUnit* unit) {
            PyObject* args = Py_BuildValue("()");
            CheckForError();
            PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(&ContextType), args);
            if (obj) {
                Context* context = reinterpret_cast<Context*>(obj);
                context->Unit = unit;
            }
            Py_DECREF(args);
            return obj;
        }
    }
}
