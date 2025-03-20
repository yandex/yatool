#include "ymake_module_adapter.h"

#include "error.h"
#include "context_class.h"
#include "convert.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/lang/plugin_facade.h>

namespace {
    using namespace NYMake::NPlugins;

    class TPluginAddParserImpl : public TParser {
    private:
        PyObject *Obj;
        std::map<TString, TString> IndDepsRule;
        bool PassInducedIncludes;

    public:
        TPluginAddParserImpl(PyObject *obj, const std::map<TString, TString> &indDepsRule, bool passInducedIncludes)
            : Obj(obj)
            , IndDepsRule(indDepsRule)
            , PassInducedIncludes(passInducedIncludes)
        {
        }

        ~TPluginAddParserImpl() override {
            TPyThreadLock pylk;
            Py_XDECREF(Obj);
        }

        void Execute(const TString &path, TPluginUnit &unit, TVector<TString> &includes,
                     TPyDictReflection &inducedDeps) override {
            TPyThreadLock pylk;
            PyObject *context = CreateContextObject(&unit);
            CheckForError();

            PyObject *argList2 = Py_BuildValue("(sO)", path.data(), context);
            Py_DecRef(context);
            CheckForError();
            PyObject *parserObj = PyObject_CallObject(Obj, argList2);
            CheckForError();
            Py_DecRef(argList2);

            PyObject *includesMethod = PyObject_GetAttrString(parserObj, "includes");
            CheckForError();
            PyObject *emptyArgs = Py_BuildValue("()");
            CheckForError();
            PyObject *pyIncludes = PyObject_CallObject(includesMethod, emptyArgs);
            CheckForError();
            Flatten(pyIncludes, includes);
            Py_DecRef(includesMethod);

            if (PyObject_HasAttrString(parserObj, "induced_deps")) {
                PyObject *inducedDepsMethod = PyObject_GetAttrString(parserObj, "induced_deps");
                CheckForError();
                PyObject *pyInducedDeps = PyObject_CallObject(inducedDepsMethod, emptyArgs);
                CheckForError();
                Flatten(pyInducedDeps, inducedDeps);
                Py_DecRef(inducedDepsMethod);
            }

            Py_DecRef(parserObj);
            Py_DecRef(emptyArgs);
        }

        bool GetPassInducedIncludes() const override {
            return PassInducedIncludes;
        }

        const std::map<TString, TString>& GetIndDepsRule() const override {
            return IndDepsRule;
        };
    };
}

void AddParser(TBuildConfiguration* conf, const TString& ext, PyObject* callable, std::map<TString, TString> inducedDeps, bool passInducedIncludes) {
    conf->RegisterPluginParser(ext, new TPluginAddParserImpl(callable, inducedDeps, passInducedIncludes));
}
