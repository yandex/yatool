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
#include <util/string/strip.h>

#include <contrib/libs/pugixml/pugixml.hpp>

#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
    TStringBuf CutLastExtension(const TStringBuf path) {
        TStringBuf left;
        TStringBuf right;
        if (path.TryRSplit('.', left, right) && !left.empty() && right.find_first_of("\\/") == right.npos) {
            return left;
        }
        return path;
    }

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

    static PyType_Slot YMakeContextTypeSlots[] = {
         {Py_tp_doc, (void*)"Context type"},
         {Py_tp_getattr, (void*)ContextTypeGetAttrFunc},
         {Py_tp_new, (void*)PyType_GenericNew},
         {0, 0}
    };

    static PyType_Spec ContextTypeSpec = {
        .name = "ymake.Context",
        .basicsize = sizeof(Context),
        .flags = Py_TPFLAGS_DEFAULT,
        .slots = YMakeContextTypeSlots,
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
            try {
                TString macroName = cmdContext->Name.substr(2);
                macroName.to_upper();
                cmdContext->Unit->CallMacro(macroName, methodArgs);
            } catch (const std::exception& e) {
                PyErr_SetString(PyExc_RuntimeError, e.what());
                return nullptr;
            }
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

    static PyType_Slot YMakeCmdContextTypeSlots[] = {
        {Py_tp_doc, (void*)"CmdContext type"},
        {Py_tp_init, (void*)CmdContextInit},
        {Py_tp_call, (void*)CmdContextCall},
        {Py_tp_new, (void*)PyType_GenericNew},
        {0, 0}
    };

    static PyType_Spec CmdContextTypeSpec = {
        .name = "ymake.Context",
        .basicsize = sizeof(CmdContext),
        .flags = Py_TPFLAGS_DEFAULT,
        .slots = YMakeCmdContextTypeSlots,
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

    PyObject* MethodGetArtifactIdFromPomXml(PyObject* /*self*/, PyObject* const* args, Py_ssize_t nargs) {
        if (nargs != 1) {
            PyErr_Format(PyExc_TypeError, "ymake.get_artifact_id_from_pom_xml takes 1 positional arguments but %z were given", nargs);
            return nullptr;
        }

        const char* data = nullptr;
        Py_ssize_t size = 0;
        PyObject* xmlDocObject{args[0]};
        TScopedPyObjectPtr asUnicode{};
        if (PyUnicode_Check(xmlDocObject)) {
            data = PyUnicode_AsUTF8AndSize(xmlDocObject, &size);
            if (data == nullptr) {
                return nullptr;
            }
        } else if (PyBytes_Check(xmlDocObject) || PyByteArray_Check(xmlDocObject)) {
            asUnicode.Reset(PyUnicode_FromEncodedObject(xmlDocObject, "utf-8", NULL));
            if (asUnicode == nullptr) {
                return nullptr;
            }
            data = PyUnicode_AsUTF8AndSize(asUnicode, &size);
            if (data == nullptr) {
                return nullptr;
            }
        } else {
            PyErr_SetString(PyExc_TypeError, "Expected string or UTF-8 encoded bytes or bytearray");
            return nullptr;
        }

        pugi::xml_document doc;
        if (doc.load_buffer(data, size)) {
            pugi::xml_node root = doc.root();
            for (auto path : {"/project/{http://maven.apache.org/POM/4.0.0}artifactId", "/project/artifactId"}) {
                pugi::xml_node node = root.first_element_by_path(path);
                if (node) {
                    return Py_BuildValue("s", node.text().get());
                }
            }
        } else {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load XML document");
            return nullptr;
        }

        Py_RETURN_NONE;
    }

    PyObject* MethodParseSsqlsFromString(PyObject* /*self*/, PyObject* const* args, Py_ssize_t nargs) {
        if (nargs != 1) {
            PyErr_Format(PyExc_TypeError, "ymake.select_attribute_values takes 1 positional arguments but %z were given", nargs);
            return nullptr;
        }

        const char* data = nullptr;
        Py_ssize_t dataSize = 0;
        PyObject* xmlDocObject{args[0]};
        TScopedPyObjectPtr dataAsUnicode{};
        if (PyUnicode_Check(xmlDocObject)) {
            data = PyUnicode_AsUTF8AndSize(xmlDocObject, &dataSize);
            if (data == nullptr) {
                return nullptr;
            }
        } else if (PyBytes_Check(xmlDocObject) || PyByteArray_Check(xmlDocObject)) {
            dataAsUnicode.Reset(PyUnicode_FromEncodedObject(xmlDocObject, "utf-8", NULL));
            if (dataAsUnicode == nullptr) {
                return nullptr;
            }
            data = PyUnicode_AsUTF8AndSize(dataAsUnicode, &dataSize);
            if (data == nullptr) {
                return nullptr;
            }
        } else {
            PyErr_SetString(PyExc_TypeError, "Expected string or UTF-8 encoded bytes or bytearray");
            return nullptr;
        }

        pugi::xml_document doc;
        if (doc.load_buffer(data, dataSize)) {
            pugi::xml_node root = doc.root();
            const pugi::xpath_node_set& includes = root.select_nodes("//include");
            const pugi::xpath_node_set& ancestors = root.select_nodes("//ancestors/ancestor[@path]");
            TVector<TString> headers;
            TVector<TString> xmls;
            for (const auto& node : includes) {
                TStringBuf include{StripString(TStringBuf{node.node().text().get()}, [](const char* pch) { return EqualToOneOf(*pch, '<', '>', '"'); })};
                if (!include.empty()) {
                    headers.push_back(TString{include});
                }

                TStringBuf xml{node.node().attribute("path").value()};
                if (!xml.empty()) {
                    xmls.push_back(TString{xml});
                }
            }

            for (const auto& node : ancestors) {
                TStringBuf xml{node.node().attribute("path").value()};
                if (!xml.empty()) {
                    xmls.push_back(TString{xml});
                    headers.push_back(TString::Join(CutLastExtension(xml), ".h"));
                }
            }

            Py_ssize_t index{0};
            TScopedPyObjectPtr xmlsList = PyList_New(xmls.size());
            for (const auto& xml : xmls) {
                if (PyList_SetItem(xmlsList, index++, Py_BuildValue("s", xml.c_str()))) {
                    return nullptr;
                }
            }

            index = 0;
            TScopedPyObjectPtr headersList = PyList_New(headers.size());
            for (const auto& header : headers) {
                if (PyList_SetItem(headersList, index++, Py_BuildValue("s", header.c_str()))) {
                    return nullptr;
                }
            }

            TScopedPyObjectPtr result = PyTuple_New(2);
            if (PyTuple_SetItem(result, 0, xmlsList.Release())) {
                return nullptr;
            }
            if (PyTuple_SetItem(result, 1, headersList.Release())) {
                return nullptr;
            }

            return result.Release();
        } else {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load XML document");
            return nullptr;
        }

        Py_RETURN_NONE;
    }

    struct YMakeState {
        PyTypeObject* ContextType;
        PyTypeObject* CmdContextType;
    };

    YMakeState* GetYMakeState(PyObject* mod) {
        YMakeState* state = (YMakeState*)PyModule_GetState(mod);
        Y_ASSERT(state != nullptr);
        return state;
    }

    int YMakeExec(PyObject* mod) {
        YMakeState* state = GetYMakeState(mod);

        state->ContextType = (PyTypeObject*)PyType_FromModuleAndSpec(mod, &ContextTypeSpec, NULL);
        if (state->ContextType == NULL) {
            return -1;
        }
        if (PyModule_AddType(mod, state->ContextType)) {
            return -1;
        }

        state->CmdContextType = (PyTypeObject*)PyType_FromModuleAndSpec(mod, &CmdContextTypeSpec, NULL);
        if (state->CmdContextType == NULL) {
            return -1;
        }
        if (PyModule_AddType(mod, state->CmdContextType)) {
            return -1;
        }

        return 0;
    }

    int YMakeTraverse(PyObject* mod, visitproc visit, void* arg) {
        YMakeState* state = GetYMakeState(mod);
        Py_VISIT(state->ContextType);
        Py_VISIT(state->CmdContextType);
        return 0;
    }

    int YMakeClear(PyObject* mod) {
        YMakeState* state = GetYMakeState(mod);
        Py_CLEAR(state->ContextType);
        Py_CLEAR(state->CmdContextType);
        return 0;
    }

    void YMakeFree(void* mod) {
        YMakeClear((PyObject*)mod);
    }

    PyMethodDef YMakeMethods[] = {
        {"add_parser", (PyCFunction)MethodAddParser, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Add a parser for files with the given extension")},
        {"report_configure_error", (PyCFunction)MethodReportConfigureError, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Report configure error")},
        {"parse_cython_includes", MethodParseCythonIncludes, METH_VARARGS, PyDoc_STR("Parse Cython includes")},
        {"get_artifact_id_from_pom_xml", (PyCFunction)MethodGetArtifactIdFromPomXml, METH_FASTCALL, PyDoc_STR("Get artifactId from pom.xml")},
        {"parse_ssqls_from_string", (PyCFunction)MethodParseSsqlsFromString, METH_FASTCALL, PyDoc_STR("Parse SSQLS")},
        {NULL, NULL, 0, NULL}};

    PyModuleDef_Slot YMakeSlots[] = {
        {Py_mod_exec, (void*)YMakeExec},
        {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
        {0, NULL}};

    struct PyModuleDef ymakemodule = {
        PyModuleDef_HEAD_INIT,           // m_base
        "ymake",                         // m_name
        PyDoc_STR("Interface to YMake"), // m_doc
        sizeof(YMakeState),              // m_size
        YMakeMethods,                    // m_methods
        YMakeSlots,                      // m_slots
        YMakeTraverse,                   // m_traverse
        YMakeClear,                      // m_clear
        YMakeFree,                       // m_free
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
        YMakeState* state = GetYMakeState(ymakeModule);
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(state->ContextType), args);
        if (obj) {
            Context* context = reinterpret_cast<Context*>(obj);
            context->Unit = unit;
        }
        return obj;
    }

    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName) {
        TScopedPyObjectPtr args = Py_BuildValue("(s)", attrName);
        TScopedPyObjectPtr ymakeModule = PyImport_ImportModule("ymake");
        CheckForError();
        YMakeState* state = GetYMakeState(ymakeModule);
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(state->CmdContextType), args);
        if (obj) {
            CmdContext* cmdContext = reinterpret_cast<CmdContext*>(obj);
            cmdContext->Unit = unit;
        }
        return obj;
    }
} // namespace NYMake::NPlugins
