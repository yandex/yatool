#include "ymake_module.h"

#include "convert.h"
#include "error.h"
#include "plugin_macro_impl.h"
#include "ymake_module_adapter.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/plugins/pybridge/raii.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/strip.h>

#include <contrib/libs/pugixml/pugixml.hpp>

#include <Python.h>

using namespace NYMake::NPlugins;

namespace {
    template<typename T>
    struct TMemberTraits {
        constexpr static  bool IsMember = false;
        constexpr static  bool IsMemberFunction = false;
    };

    template<typename C, typename M>
    struct TMemberTraits<M C::*> {
        constexpr static  bool IsMember = true;
        constexpr static  bool IsMemberFunction = std::is_function_v<M>;

        using TClass = C;
        using TType = M;
    };

    template<typename T>
    concept MemberFunction = TMemberTraits<T>::IsMemberFunction;

    template<MemberFunction auto Member>
    PyObject* WrapMember(PyObject* self, PyObject* const* args, Py_ssize_t nargs) noexcept {
        Y_ASSERT(self);
        Y_ASSERT(PyModule_Check(self));
        auto* obj = static_cast<TMemberTraits<decltype(Member)>::TClass*>(PyModule_GetState(self));
        return (obj->*Member)(std::span{args, static_cast<size_t>(nargs)});
    }

    TStringBuf CutLastExtension(const TStringBuf path) noexcept {
        TStringBuf left;
        TStringBuf right;
        if (path.TryRSplit('.', left, right) && !left.empty() && right.find_first_of("\\/") == right.npos) {
            return left;
        }
        return path;
    }

    struct Context {
        PyObject_HEAD
        TPluginUnit* Unit;
    };

    PyObject* ContextTypeGetAttrFunc(PyObject* self, char* attrname) {
        Context* context = reinterpret_cast<Context*>(self);
        PyObject* obj = CreateCmdContextObject(context->Unit, attrname);
        CheckForError();
        return obj;
    }

    PyType_Slot YMakeContextTypeSlots[] = {
         {Py_tp_doc, (void*)"Context type"},
         {Py_tp_getattr, (void*)ContextTypeGetAttrFunc},
         {Py_tp_new, (void*)PyType_GenericNew},
         {0, 0}
    };

    PyType_Spec ContextTypeSpec = {
        .name = "ymake.Context",
        .basicsize = sizeof(Context),
        .flags = Py_TPFLAGS_DEFAULT,
        .slots = YMakeContextTypeSlots,
    };

    struct CmdContext {
        PyObject_HEAD
        std::string Name;
        TPluginUnit* Unit;
    };

    PyObject* CmdContextCall(PyObject* self, PyObject* args, PyObject* /*kwargs*/) {
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
            return NPyBind::BuildPyObject(cmdContext->Unit->Enabled(methodArgs[0]));
        } else if (cmdContext->Name == TStringBuf("get") || cmdContext->Name == TStringBuf("get_nosubst")) { // get var value without substs
            Y_ABORT_UNLESS(methodArgs.size() == 1);
            return NPyBind::BuildPyObject(cmdContext->Unit->Get(methodArgs[0]));
        } else if (cmdContext->Name == TStringBuf("get_subst")) { // get var value with subst all vars
            Y_ABORT_UNLESS(methodArgs.size() == 1);
            auto value = cmdContext->Unit->GetSubst(methodArgs[0]);
            if (std::holds_alternative<TStringBuf>(value)) {
                return NPyBind::BuildPyObject(std::get<TStringBuf>(value)); // if !value.IsInited() - return None in Python
            } else {
                return NPyBind::BuildPyObject(std::get<TString>(value));
            }
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

    int CmdContextInit(CmdContext* self, PyObject* args, PyObject* kwds) {
        const char* str;

        static char* kwlist[] = {const_cast<char*>("name"), nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &str)) {
            return -1;
        }
        self->Name = str;

        return 0;
    }

    PyType_Slot YMakeCmdContextTypeSlots[] = {
        {Py_tp_doc, (void*)"CmdContext type"},
        {Py_tp_init, (void*)CmdContextInit},
        {Py_tp_call, (void*)CmdContextCall},
        {Py_tp_new, (void*)PyType_GenericNew},
        {0, 0}
    };

    PyType_Spec CmdContextTypeSpec = {
        .name = "ymake.Context",
        .basicsize = sizeof(CmdContext),
        .flags = Py_TPFLAGS_DEFAULT,
        .slots = YMakeCmdContextTypeSlots,
    };

    PyObject* MethodAddParser(PyObject* /*self*/, PyObject* args, PyObject* kwargs) {
        const char* ext;
        PyObject* confObj;
        PyObject* callableObj;
        PyObject* inducedDepsObj = nullptr;
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
            return nullptr;
        }
        TBuildConfiguration* conf = nullptr;
        if (PyCapsule_CheckExact(confObj)) {
            conf = reinterpret_cast<TBuildConfiguration*>(PyCapsule_GetPointer(confObj, "BuildConfiguration"));
        }
        if (conf == nullptr) {
            PyErr_SetString(PyExc_TypeError, "first argument of ymake.add_parser is expected to be a PyCapsule object containing a pointer to TBuildConfiguration");
            return nullptr;
        }
        std::map<TString, TString> inducedDeps;
        if (inducedDepsObj) {
            if (!PyDict_Check(inducedDepsObj)) {
                PyErr_SetString(PyExc_TypeError, "'induced' argument of ymake.add_parser is expected to be of type 'dict'");
                return nullptr;
            }

            Py_ssize_t pos = 0;
            PyObject* keyObj = nullptr;
            PyObject* valueObj = nullptr;
            while (PyDict_Next(inducedDepsObj, &pos, &keyObj, &valueObj)) {
                if (!PyUnicode_Check(keyObj)) {
                    PyErr_SetString(PyExc_TypeError, "key of dict (of 'induced' argument) must be a string");
                    return nullptr;
                }
                const char* key = PyUnicode_AsUTF8AndSize(keyObj, nullptr);
                if (!key || PyErr_Occurred()) {
                    return nullptr;
                }
                if (!PyUnicode_Check(valueObj)) {
                    PyErr_SetString(PyExc_TypeError, "value of dict (of 'induced' argument) must be a string");
                    return nullptr;
                }
                const char* value = PyUnicode_AsUTF8AndSize(valueObj, nullptr);
                if (!value || PyErr_Occurred()) {
                    return nullptr;
                }
                inducedDeps.emplace(key, value);
            }
        }
        AddParser(conf, ext, callableObj, inducedDeps, passInducedIncludes);
        Py_RETURN_NONE;
    }

    PyObject* MethodReportConfigureError(PyObject* /*self*/, PyObject* args) {
        const char* errorMessage = nullptr;
        if (!PyArg_ParseTuple(args, "s:ymake.report_configure_error", &errorMessage) || PyErr_Occurred()) {
            return nullptr;
        }
        OnConfigureError(errorMessage);
        Py_RETURN_NONE;
    }

    PyObject* MethodParseCythonIncludes(PyObject* /*self*/, PyObject* args) {
        const char* data;
        if (!PyArg_ParseTuple(args, "y:ymake.parse_cython_includes", &data) || PyErr_Occurred()) {
            return nullptr;
        }
        TVector<TString> includes;
        ParseCythonIncludes(data, includes);
        Py_ssize_t size = static_cast<Py_ssize_t>(includes.size());
        NYMake::NPy::OwnedRef list{PyList_New(size)};
        for (Py_ssize_t index = 0; index < size; ++index) {
            if (PyList_SetItem(list.get(), index, Py_BuildValue("y", includes[index].data())) < 0 || PyErr_Occurred()) {
                return nullptr;
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
        NYMake::NPy::OwnedRef asUnicode{};
        if (PyUnicode_Check(xmlDocObject)) {
            data = PyUnicode_AsUTF8AndSize(xmlDocObject, &size);
            if (data == nullptr) {
                return nullptr;
            }
        } else if (PyBytes_Check(xmlDocObject) || PyByteArray_Check(xmlDocObject)) {
            asUnicode.Reset(PyUnicode_FromEncodedObject(xmlDocObject, "utf-8", nullptr));
            if (asUnicode == nullptr) {
                return nullptr;
            }
            data = PyUnicode_AsUTF8AndSize(asUnicode.get(), &size);
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
        NYMake::NPy::OwnedRef dataAsUnicode{};
        if (PyUnicode_Check(xmlDocObject)) {
            data = PyUnicode_AsUTF8AndSize(xmlDocObject, &dataSize);
            if (data == nullptr) {
                return nullptr;
            }
        } else if (PyBytes_Check(xmlDocObject) || PyByteArray_Check(xmlDocObject)) {
            dataAsUnicode.Reset(PyUnicode_FromEncodedObject(xmlDocObject, "utf-8", nullptr));
            if (dataAsUnicode == nullptr) {
                return nullptr;
            }
            data = PyUnicode_AsUTF8AndSize(dataAsUnicode.get(), &dataSize);
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
            NYMake::NPy::OwnedRef xmlsList{PyList_New(xmls.size())};
            for (const auto& xml : xmls) {
                if (PyList_SetItem(xmlsList.get(), index++, Py_BuildValue("s", xml.c_str()))) {
                    return nullptr;
                }
            }

            index = 0;
            NYMake::NPy::OwnedRef headersList{PyList_New(headers.size())};
            for (const auto& header : headers) {
                if (PyList_SetItem(headersList.get(), index++, Py_BuildValue("s", header.c_str()))) {
                    return nullptr;
                }
            }

            NYMake::NPy::OwnedRef result{PyTuple_New(2)};
            if (PyTuple_SetItem(result.get(), 0, xmlsList.Release())) {
                return nullptr;
            }
            if (PyTuple_SetItem(result.get(), 1, headersList.Release())) {
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
        NYMake::NPy::OwnedRef<PyTypeObject> ContextType;
        NYMake::NPy::OwnedRef<PyTypeObject> CmdContextType;
        TBuildConfiguration* Conf = nullptr;

        int Clear() noexcept {
            Conf = nullptr;
            ContextType.Reset();
            CmdContextType.Reset();
            return 0;
        }

        int Traverse(visitproc visit, void* arg) noexcept {
            Py_VISIT(ContextType.Get());
            Py_VISIT(CmdContextType.Get());
            return 0;
        }

        PyObject* MacroDecorator(std::span<PyObject* const> args) {
            Y_ASSERT(Conf);

            if (args.size() != 1 || !PyFunction_Check(args[0])) {
                PyErr_SetString(PyExc_RuntimeError, "ymake.macro decorator expects single function to register as a macro");
                return nullptr;
            }

            TString macroName;
            {
                NYMake::NPy::OwnedRef name{PyObject_GetAttrString(args[0], "__name__")};
                Y_ASSERT(PyUnicode_Check(name.Get()));
                Py_ssize_t size;
                const char *data = PyUnicode_AsUTF8AndSize(name.get(), &size);
                if (!data) {
                    Y_ASSERT(PyErr_Occurred());
                    return nullptr;
                }
                macroName = ToUpperUTF8(TStringBuf{data, static_cast<size_t>(size)});
            }

            PyObject* signature = PyFunction_GetAnnotations(args[0]);
            if (!signature) {
                PyErr_SetString(PyExc_RuntimeError, "ymake.macro decorator requires type hint annotations on decorated function");
                return nullptr;
            }

            NYMake::NPlugins::RegisterMacro(*Conf, macroName, NYMake::NPy::FromBorrowedRef(args[0]));

            Py_INCREF(args[0]);
            return args[0];
        }
    };

    YMakeState* GetYMakeState(PyObject* mod) noexcept {
        YMakeState* state = static_cast<YMakeState*>(PyModule_GetState(mod));
        Y_ASSERT(state != nullptr);
        return state;
    }

    YMakeState* CreateYMakeState(PyObject* mod) {
        void* stateMem = PyModule_GetState(mod);
        Y_ASSERT(stateMem != nullptr);
        return new(stateMem) YMakeState{};
    }

    int YMakeExec(PyObject* mod) {
        YMakeState* state = CreateYMakeState(mod);

        state->ContextType.Reset(reinterpret_cast<PyTypeObject*>(
            PyType_FromModuleAndSpec(mod, &ContextTypeSpec, nullptr)
        ));
        if (state->ContextType == nullptr) {
            return -1;
        }
        if (PyModule_AddType(mod, state->ContextType.Get())) {
            return -1;
        }

        state->CmdContextType.Reset(reinterpret_cast<PyTypeObject*>(
            PyType_FromModuleAndSpec(mod, &CmdContextTypeSpec, nullptr)
        ));
        if (state->CmdContextType == nullptr) {
            return -1;
        }
        if (PyModule_AddType(mod, state->CmdContextType.Get())) {
            return -1;
        }

        return 0;
    }

    int YMakeTraverse(PyObject* mod, visitproc visit, void* arg) noexcept {
        return GetYMakeState(mod)->Traverse(visit,arg);
    }

    int YMakeClear(PyObject* mod) noexcept {
        return GetYMakeState(mod)->Clear();
    }

    void YMakeFree(void* mod) noexcept {
        GetYMakeState(static_cast<PyObject*>(mod))->~YMakeState();
    }

    PyMethodDef YMakeMethods[] = {
        {"add_parser", (PyCFunction)MethodAddParser, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Add a parser for files with the given extension")},
        {"macro", (PyCFunction)WrapMember<&YMakeState::MacroDecorator>, METH_FASTCALL, PyDoc_STR("Register function as ya.make macro")},
        {"report_configure_error", (PyCFunction)MethodReportConfigureError, METH_VARARGS, PyDoc_STR("Report configure error")},
        {"parse_cython_includes", MethodParseCythonIncludes, METH_VARARGS, PyDoc_STR("Parse Cython includes")},
        {"get_artifact_id_from_pom_xml", (PyCFunction)MethodGetArtifactIdFromPomXml, METH_FASTCALL, PyDoc_STR("Get artifactId from pom.xml")},
        {"parse_ssqls_from_string", (PyCFunction)MethodParseSsqlsFromString, METH_FASTCALL, PyDoc_STR("Parse SSQLS")},
        {nullptr, nullptr, 0, nullptr}};

    PyModuleDef_Slot YMakeSlots[] = {
        {Py_mod_exec, (void*)YMakeExec},
        {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
        {0, nullptr}};

    PyModuleDef ymakemodule = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "ymake",
        .m_doc = PyDoc_STR("Interface to YMake"),
        .m_size = sizeof(YMakeState),
        .m_methods = YMakeMethods,
        .m_slots = YMakeSlots,
        .m_traverse = YMakeTraverse,
        .m_clear = YMakeClear,
        .m_free = YMakeFree,
    };
} // anonymous namespace

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake() {
        return PyModuleDef_Init(&ymakemodule);
    }

    void BindYmakeConf(TBuildConfiguration& conf) {
        NYMake::NPy::OwnedRef mod{PyImport_ImportModule("ymake")};
        Y_ASSERT(GetYMakeState(mod.get())->Conf == nullptr);
        GetYMakeState(mod.get())->Conf = &conf;
    }

    PyObject* CreateContextObject(TPluginUnit* unit) {
        NYMake::NPy::OwnedRef args{Py_BuildValue("()")};
        NYMake::NPy::OwnedRef ymakeModule{PyImport_ImportModule("ymake")};
        CheckForError();
        YMakeState* state = GetYMakeState(ymakeModule.get());
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(state->ContextType.Get()), args.get());
        if (obj) {
            Context* context = reinterpret_cast<Context*>(obj);
            context->Unit = unit;
        }
        return obj;
    }

    PyObject* CreateCmdContextObject(TPluginUnit* unit, const char* attrName) {
        NYMake::NPy::OwnedRef args{Py_BuildValue("(s)", attrName)};
        NYMake::NPy::OwnedRef ymakeModule{PyImport_ImportModule("ymake")};
        CheckForError();
        YMakeState* state = GetYMakeState(ymakeModule.get());
        PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(state->CmdContextType.Get()), args.get());
        if (obj) {
            CmdContext* cmdContext = reinterpret_cast<CmdContext*>(obj);
            cmdContext->Unit = unit;
        }
        return obj;
    }
} // namespace NYMake::NPlugins
