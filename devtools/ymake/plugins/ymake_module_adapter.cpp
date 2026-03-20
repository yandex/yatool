#include "ymake_module_adapter.h"
#include "ymake_module.h"

#include "error.h"
#include "convert.h"

#include <devtools/ymake/conf.h>
#include <devtools/ymake/plugins/pybridge/raii.h>
#include <devtools/ymake/lang/plugin_facade.h>

namespace {
    using namespace NYMake::NPlugins;

    class TPluginAddParserImpl : public TParser {
    private:
        NYMake::NPy::OwnedRef<PyObject> Obj_;
        std::map<TString, TString> IndDepsRule_;
        bool PassInducedIncludes_ = false;

    public:
        TPluginAddParserImpl(PyObject *obj, const std::map<TString, TString> &indDepsRule, bool passInducedIncludes)
            : Obj_{NYMake::NPy::FromBorrowedRef(obj)}
            , IndDepsRule_(indDepsRule)
            , PassInducedIncludes_(passInducedIncludes)
        {
        }

        void Execute(const TString &path, TPluginUnit &unit, TVector<TString> &includes,
                     TPyDictReflection &inducedDeps) override {
            PyObject *argList2 = Py_BuildValue("(sO)", path.data(), CreateContextObject(&unit).get());
            CheckForError();
            NYMake::NPy::OwnedRef<PyObject> parserObj{PyObject_CallObject(Obj_.get(), argList2)};
            CheckForError();
            Py_DecRef(argList2);

            PyObject *includesMethod = PyObject_GetAttrString(parserObj.get(), "includes");
            CheckForError();
            NYMake::NPy::OwnedRef<PyObject> emptyArgs{Py_BuildValue("()")};
            CheckForError();
            PyObject *pyIncludes = PyObject_CallObject(includesMethod, emptyArgs.get());
            CheckForError();
            Flatten(pyIncludes, includes);
            Py_DecRef(includesMethod);

            if (PyObject_HasAttrString(parserObj.get(), "induced_deps")) {
                NYMake::NPy::OwnedRef<PyObject> inducedDepsMethod{PyObject_GetAttrString(parserObj.get(), "induced_deps")};
                CheckForError();
                PyObject *pyInducedDeps = PyObject_CallObject(inducedDepsMethod.get(), emptyArgs.get());
                CheckForError();
                Flatten(pyInducedDeps, inducedDeps);
            }
        }

        bool GetPassInducedIncludes() const override {
            return PassInducedIncludes_;
        }

        const std::map<TString, TString>& GetIndDepsRule() const override {
            return IndDepsRule_;
        };
    };
}

void AddParser(TBuildConfiguration* conf, const TString& ext, PyObject* callable, std::map<TString, TString> inducedDeps, bool passInducedIncludes) {
    conf->RegisterPluginParser(ext, new TPluginAddParserImpl(callable, inducedDeps, passInducedIncludes));
}
