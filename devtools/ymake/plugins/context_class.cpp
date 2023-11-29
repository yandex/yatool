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
            PyObject* argList = Py_BuildValue("(s)", attrname);
            PyObject* obj = CmdContextCall(context->Unit, argList);
            CheckForError();
            Py_DECREF(argList);
            return obj;
        }

        static PyTypeObject ContextType = {
            .ob_base=PyVarObject_HEAD_INIT(nullptr, 0)
            .tp_name="ymake.Context",
            .tp_basicsize=sizeof(Context),
            .tp_itemsize=0,
            .tp_dealloc=nullptr,
            .tp_vectorcall_offset=0,
            .tp_getattr=ContextTypeGetAttrFunc,
            .tp_setattr=nullptr,
            .tp_as_async=nullptr,
            .tp_repr=nullptr,
            .tp_as_number=nullptr,
            .tp_as_sequence=nullptr,
            .tp_as_mapping=nullptr,
            .tp_hash=nullptr,
            .tp_call=nullptr,
            .tp_str=nullptr,
            .tp_getattro=nullptr,
            .tp_setattro=nullptr,
            .tp_as_buffer=nullptr,
            .tp_flags=Py_TPFLAGS_DEFAULT,
            .tp_doc="Context objects",
            .tp_traverse=nullptr,
            .tp_clear=nullptr,
            .tp_richcompare=nullptr,
            .tp_weaklistoffset=0,
            .tp_iter=nullptr,
            .tp_iternext=nullptr,
            .tp_methods=nullptr,
            .tp_members=nullptr,
            .tp_getset=nullptr,
            .tp_base=nullptr,
            .tp_dict=nullptr,
            .tp_descr_get=nullptr,
            .tp_descr_set=nullptr,
            .tp_dictoffset=0,
            .tp_init=nullptr,
            .tp_alloc=nullptr,
            .tp_new=nullptr,
            .tp_free=nullptr,
            .tp_is_gc=nullptr,
            .tp_bases=nullptr,
            .tp_mro=nullptr,
            .tp_cache=nullptr,
            .tp_subclasses=nullptr,
            .tp_weaklist=nullptr,
            .tp_del=nullptr,
            .tp_version_tag=0,
            .tp_finalize=nullptr,
            .tp_vectorcall=nullptr,
        };

        bool ContextTypeInit(PyObject* ymakeModule) {
            ContextType.tp_new = PyType_GenericNew;
            if (PyType_Ready(&ContextType) < 0)
                return false;
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
    }
}
