#include "ymake_module.h"

#include "convert.h"
#include "error.h"
#include "plugin_macro_impl.h"
#include "ymake_module_adapter.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/include_parsers/cython_parser.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/plugins/pybridge/lambda.h>
#include <devtools/ymake/plugins/pybridge/raii.h>
#include <devtools/ymake/plugins/pybridge/str.h>
#include <devtools/ymake/plugins/pybridge/ffi_macro.h>

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

    template<typename T, typename... Args>
    concept MemberFunction = TMemberTraits<T>::IsMemberFunction && std::invocable<T, typename TMemberTraits<T>::TClass&, Args...>;

    template<MemberFunction<std::span<PyObject*>> auto Member>
    PyObject* WrapMember(PyObject* self, PyObject* const* args, Py_ssize_t nargs) noexcept {
        Y_ASSERT(self);
        Y_ASSERT(PyModule_Check(self));
        auto* obj = static_cast<TMemberTraits<decltype(Member)>::TClass*>(PyModule_GetState(self));
        return (obj->*Member)(std::span{args, static_cast<size_t>(nargs)});
    }

    template<MemberFunction<PyObject*, PyObject*> auto Member>
    PyObject* WrapMember(PyObject* self, PyObject* args, PyObject* kwargs) noexcept {
        Y_ASSERT(self);
        Y_ASSERT(PyModule_Check(self));
        auto* obj = static_cast<TMemberTraits<decltype(Member)>::TClass*>(PyModule_GetState(self));
        return (obj->*Member)(args, kwargs);
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

        PyObject* CreateCmdContextObject(const char* attrName);
    };

    PyObject* ContextTypeGetAttrFunc(PyObject* self, char* attrname) {
        Context* context = reinterpret_cast<Context*>(self);
        PyObject* obj = context->CreateCmdContextObject(attrname);
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
        .name = "ymake.Unit",
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

    bool ParseInducedDepsArg(PyObject* inducedDepsObj, std::map<TString, TString>& inducedDeps) {
        if (!inducedDepsObj)
            return true;

        if (!PyDict_Check(inducedDepsObj)) {
            PyErr_SetString(PyExc_TypeError, "'induced' argument of ymake.add_parser is expected to be of type 'dict'");
            return false;
        }

        Py_ssize_t pos = 0;
        PyObject* keyObj = nullptr;
        PyObject* valueObj = nullptr;
        while (PyDict_Next(inducedDepsObj, &pos, &keyObj, &valueObj)) {
            if (!PyUnicode_Check(keyObj)) {
                PyErr_SetString(PyExc_TypeError, "key of dict (of 'induced' argument) must be a string");
                return false;
            }
            const char* key = PyUnicode_AsUTF8AndSize(keyObj, nullptr);
            if (!key || PyErr_Occurred()) {
                return false;
            }
            if (!PyUnicode_Check(valueObj)) {
                PyErr_SetString(PyExc_TypeError, "value of dict (of 'induced' argument) must be a string");
                return false;
            }
            const char* value = PyUnicode_AsUTF8AndSize(valueObj, nullptr);
            if (!value || PyErr_Occurred()) {
                return false;
            }
            inducedDeps.emplace(key, value);
        }
        return true;
    }

    class TYMakeMod {
    public:
        TYMakeMod(
            NYMake::NPy::OwnedRef<PyTypeObject>&& contextType,
            NYMake::NPy::OwnedRef<PyTypeObject>&& cmdContextType
        ) noexcept
            : ContextType_(std::move(contextType))
            , CmdContextType_{std::move(cmdContextType)}
        {}

        void BindConf(TBuildConfiguration& conf) {
            Y_ASSERT(!Conf_);
            Conf_ = &conf;
        }

        NYMake::NPy::OwnedRef<> CreateContextObject(TPluginUnit* unit) {
            PyObject* obj = PyObject_CallObject(reinterpret_cast<PyObject*>(ContextType_.Get()), EmptyTuple_.get());
            if (obj) {
                Context* context = reinterpret_cast<Context*>(obj);
                context->Unit = unit;
            }
            CheckForError();
            return NYMake::NPy::OwnedRef<PyObject>{obj};
        }

        NYMake::NPy::OwnedRef<> CreateCmdContextObject(const char* attrName) {
            NYMake::NPy::OwnedRef args{Py_BuildValue("(s)", attrName)};
            CheckForError();
            return NYMake::NPy::OwnedRef{PyObject_CallObject(reinterpret_cast<PyObject*>(CmdContextType_.Get()), args.get())};
        }

        int Clear() noexcept {
            Conf_ = nullptr;
            ContextType_.Reset();
            CmdContextType_.Reset();
            return 0;
        }

        int Traverse(visitproc visit, void* arg) noexcept {
            Py_VISIT(ContextType_.Get());
            Py_VISIT(CmdContextType_.Get());
            return 0;
        }

        PyObject* ParserDecorator(PyObject* args, PyObject* kwargs) {
            const char* ext;
            PyObject* inducedDepsObj = nullptr;
            int passInducedIncludes = 0;
            const char* keys[] = {
                "",
                "induced",
                "pass_induced_includes",
                nullptr
            };
            if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$Op:ymake.parser", keys, &ext, &inducedDepsObj, &passInducedIncludes))
                return nullptr;
            std::map<TString, TString> inducedDeps;
            if (!ParseInducedDepsArg(inducedDepsObj, inducedDeps))
                return nullptr;

            return NYMake::NPy::MakePyLambda([this, ext=TString{ext}, inducedDeps=std::move(inducedDeps), passInducedIncludes](std::span<PyObject* const> args) -> PyObject* {
                if (args.size() != 1) {
                    PyErr_SetString(PyExc_RuntimeError, "ymake.parser decorator expects single decorated class to register as a parser");
                    return nullptr;
                }
                if (Conf_) {
                    AddParser(Conf_, ext, args[0], inducedDeps, passInducedIncludes);
                } else {
                    // Using YErr() here since it will fail build for case when this error happens inside ymake application but will not
                    // fail python code which tries to import plugins with ymake module being used as regular python module.
                    YErr() << "ymake.parser decorator called without active build configuration! Parser will not be registered!" << Endl;
                }
                Py_INCREF(args[0]);
                return args[0];
            }).Release();
        }

        PyObject* MacroDecorator(PyObject* args, PyObject* kwargs) {
            PyObject* func = nullptr;
            if (!PyArg_ParseTuple(args, "|O", &func))
                return nullptr;

            PyObject* ignoredArgs = nullptr;
            const char* keys[] = {"ignored_args", nullptr};
            if (!PyArg_ParseTupleAndKeywords(EmptyTuple_.get(), kwargs, "|$O:ymake.macro", keys, &ignoredArgs))
                return nullptr;
            if (ignoredArgs && !PySet_Check(ignoredArgs)) {
                PyErr_Format(PyExc_TypeError, "ymake.macro decorator 'ignored_args' expected to be a set of strings but got '%N'.", Py_TYPE(ignoredArgs));
                return nullptr;
            }

            THashSet<std::string> ignore = ignoredArgs ? ConvertStrSet(*ignoredArgs) : THashSet<std::string>{};
            if (func)
                return DoDecorateMacro(func, ignore);

            return NYMake::NPy::MakePyLambda([this, ignore = std::move(ignore)](std::span<PyObject* const> args) -> PyObject* {
                if (args.size() != 1) {
                    PyErr_SetString(PyExc_RuntimeError, "ymake.macro decorator expects single decorated function to register as a macro");
                    return nullptr;
                }
                return DoDecorateMacro(args[0], ignore);
            }).Release();
        }

    private:
        THashSet<std::string> ConvertStrSet(PyObject& pySet) const {
            THashSet<std::string> res;

            NYMake::NPy::OwnedRef iter{PyObject_GetIter(&pySet)};
            PyObject* item = nullptr;
            while ((item = PyIter_Next(iter.get())) != nullptr) {
                NYMake::NPy::OwnedRef itemRef{std::exchange(item, nullptr)};
                if (!PyUnicode_Check(itemRef.get())) {
                    PyErr_Format(PyExc_TypeError, "ymake.macro decorator 'ignored_args' expected to be a set of strings but got item of type '%N'.", Py_TYPE(itemRef.get()));
                    break;
                }

                res.insert(std::string{NYMake::NPy::StrContent(*itemRef)});
            }
            return res;
        }

        PyObject* DoDecorateMacro(PyObject* func, const THashSet<std::string>& ignoreArgs = {}) {
            if (!func || !PyFunction_Check(func)) {
                PyErr_SetString(PyExc_RuntimeError, "ymake.macro decorator expects single function to register as a macro");
                return nullptr;
            }

            auto macro = NYMake::NPy::TFFIMacro::Wrap(NYMake::NPy::FromBorrowedRef(func), *ContextType_, ignoreArgs);
            if (!macro.has_value()) {
                using enum NYMake::NPy::ESignatureDeductionError;
                switch (macro.error()) {
                    case MissingTypeHints:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro requires type hints on decorated function.");
                        break;
                    case MissingUnitArg:
                        PyErr_Format(PyExc_RuntimeError, "ymake.macro: first argument type must be '%N'.", ContextType_.get());
                        break;
                    case WrongArgType:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: only bool, str or tuple[str, ...] types are allowed for macro arguments.");
                        break;
                    case WrongReturnType:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: only None is allowed for macro retrun type.");
                        break;
                    case WrongFlagDefault:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: only False is allowed as default value of a flag (bool) KW argument.");
                        break;
                    case PositionalAfterVararg:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: only last (vararg) positional argument can be a tuple.");
                        break;
                    case KwArgWithoutDefaults:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: all kw-only arguments must have default values.");
                        break;
                    case IndistinguishableKwArg:
                        PyErr_SetString(PyExc_RuntimeError, "ymake.macro: non kw-only arguments are not allowed to have default values.");
                        break;
                    case PyException:
                        break;
                }
                return nullptr;
            }

            if (Conf_) {
                NYMake::NPlugins::RegisterMacro(*Conf_, std::move(macro.value()));
            } else {
                // Using YErr() here since it will fail build for case when this error happens inside ymake application but will not
                // fail python code which tries to import plugins with ymake module being used as regular python module.
                YErr() << "ymake.macro decorator called without active build configuration! Macro will not be registered!" << Endl;
            }

            Py_INCREF(func);
            return func;
        }

    private:
        NYMake::NPy::OwnedRef<PyTypeObject> ContextType_;
        NYMake::NPy::OwnedRef<PyTypeObject> CmdContextType_;
        NYMake::NPy::OwnedRef<> EmptyTuple_{PyTuple_New(0)};
        TBuildConfiguration* Conf_ = nullptr;
    };

    TYMakeMod* GetYMakeState(PyObject* mod) noexcept {
        TYMakeMod* state = static_cast<TYMakeMod*>(PyModule_GetState(mod));
        Y_ASSERT(state != nullptr);
        return state;
    }

    int YMakeExec(PyObject* mod) {
        NYMake::NPy::OwnedRef<PyTypeObject> contextType{reinterpret_cast<PyTypeObject*>(
            PyType_FromModuleAndSpec(mod, &ContextTypeSpec, nullptr)
        )};
        if (contextType == nullptr) {
            return -1;
        }
        if (PyModule_AddType(mod, contextType.Get())) {
            return -1;
        }

        NYMake::NPy::OwnedRef<PyTypeObject> cmdContextType{reinterpret_cast<PyTypeObject*>(
            PyType_FromModuleAndSpec(mod, &CmdContextTypeSpec, nullptr)
        )};
        if (cmdContextType == nullptr) {
            return -1;
        }
        if (PyModule_AddType(mod, cmdContextType.Get())) {
            return -1;
        }

        void* stateMem = PyModule_GetState(mod);
        Y_ASSERT(stateMem != nullptr);
        new(stateMem) TYMakeMod{std::move(contextType), std::move(cmdContextType)};

        return 0;
    }

    int YMakeTraverse(PyObject* mod, visitproc visit, void* arg) noexcept {
        return GetYMakeState(mod)->Traverse(visit,arg);
    }

    int YMakeClear(PyObject* mod) noexcept {
        return GetYMakeState(mod)->Clear();
    }

    void YMakeFree(void* mod) noexcept {
        GetYMakeState(static_cast<PyObject*>(mod))->~TYMakeMod();
    }

    PyMethodDef YMakeMethods[] = {
        {"parser", (PyCFunction)WrapMember<&TYMakeMod::ParserDecorator>, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Use class as a parser for files with the given extension")},
        {"macro", (PyCFunction)WrapMember<&TYMakeMod::MacroDecorator>, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Register function as ya.make macro")},
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
        .m_size = sizeof(TYMakeMod),
        .m_methods = YMakeMethods,
        .m_slots = YMakeSlots,
        .m_traverse = YMakeTraverse,
        .m_clear = YMakeClear,
        .m_free = YMakeFree,
    };

    PyObject* Context::CreateCmdContextObject(const char* attrName) {
        NYMake::NPy::OwnedRef ymakeModule{PyImport_ImportModule("ymake")};
        CheckForError();
        auto obj = GetYMakeState(ymakeModule.get())->CreateCmdContextObject(attrName);
        if (obj) {
            CmdContext* cmdContext = reinterpret_cast<CmdContext*>(obj.get());
            cmdContext->Unit = Unit;
        }
        return obj.Release();
    }
} // anonymous namespace

namespace NYMake::NPlugins {
    PyMODINIT_FUNC PyInit_ymake() {
        return PyModuleDef_Init(&ymakemodule);
    }

    void BindYmakeConf(TBuildConfiguration& conf) {
        NYMake::NPy::OwnedRef mod{PyImport_ImportModule("ymake")};
        GetYMakeState(mod.get())->BindConf(conf);
    }

    NYMake::NPy::OwnedRef<PyObject> CreateContextObject(TPluginUnit* unit) {
        NYMake::NPy::OwnedRef ymakeModule{PyImport_ImportModule("ymake")};
        CheckForError();
        return GetYMakeState(ymakeModule.get())->CreateContextObject(unit);
    }
} // namespace NYMake::NPlugins
