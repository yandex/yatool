#include "scoped_py_object_ptr.h"
#include "ymake_module_adapter.h"

#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/lang/plugin_facade.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
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

    PyMethodDef YMakeMethods[] = {
        {"add_parser", (PyCFunction)MethodAddParser, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Add a parser for files with the given extension")},
        {"report_configure_error", (PyCFunction)MethodReportConfigureError, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Report configure error")},
        {"parse_cython_includes", MethodParseCythonIncludes, METH_VARARGS, PyDoc_STR("Parse Cython includes")},
        {NULL, NULL, 0, NULL}
    };

    struct PyModuleDef ymakemodule = {
        PyModuleDef_HEAD_INIT,           // m_base
        "ymake",                         // m_name
        PyDoc_STR("Interface to YMake"), // m_doc
        -1,                              // m_size
        YMakeMethods,                    // m_methods
        NULL,                            // m_slots is set to NULL for single-phase initialization
        NULL,                            // m_traverse
        NULL,                            // m_clear
        NULL,                            // m_free
    };
} // anonymous namespace


PyMODINIT_FUNC PyInit_ymake(void) {
    return PyModule_Create(&ymakemodule);
}
