import six

from libcpp.string cimport string
from libcpp.map cimport map
from libcpp cimport bool
from util.generic.vector cimport TVector

cdef extern from "Python.h":
    ctypedef struct PyObject


cdef extern from "util/generic/string.h":
    ctypedef string TString


cdef extern from "devtools/ymake/lang/plugin_facade.h":
    void OnPluginLoadFail(const char*, const char*)
    void OnConfigureError(const char*)
    void OnBadDirError(const char*, const char*)


cdef extern from "devtools/ymake/plugins/plugin_macro_impl.h" namespace "NYMake::NPlugins":
    cdef void RegisterMacro(const char* name, PyObject* func)


cdef extern from "devtools/ymake/plugins/ymake_module_adapter.h":
    void AddParser(const TString& ext, PyObject* callable, map[TString, TString] inducedDeps, bool passInducedIncludes)


cdef extern from "devtools/ymake/include_parsers/cython_parser.h":
    void ParseCythonIncludes(const TString& data, TVector[TString]& includes)


def addparser(ext, parser, induced=None, pass_induced_includes=False):
    if induced is None:
        induced = {}
    AddParser(six.ensure_binary(ext), <PyObject*>parser, induced, pass_induced_includes)


def report_configure_error(error, missing_dir=None):
    if missing_dir is not None:
        OnBadDirError(six.ensure_binary(error.format(missing_dir)), six.ensure_binary(missing_dir))
    else:
        OnConfigureError(six.ensure_binary(error))


def parse_cython_includes(data):
    cdef TVector[TString] includes;
    ParseCythonIncludes(data, includes)
    return list(includes)
