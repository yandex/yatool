import os
import six
import sys
import inspect


cdef extern from "devtools/ymake/lang/plugin_facade.h":
    cdef void RegisterPluginFilename(const char* fileName)
    cdef void OnPluginLoadFail(const char* fileName, const char* msg)


cdef extern from "Python.h":
    ctypedef struct PyObject


cdef extern from "devtools/ymake/plugins/plugin_macro_impl.h" namespace "NYMake::NPlugins":
    cdef void RegisterMacro(const char* name, PyObject* func)


cdef PyObject *ptr


cdef public void load_plugins(const char* s):
    # The order of plugin roots does really matter - 'build/plugins' should go first
    sys.path.insert(0, six.ensure_str(s))
    # We register plugins macros from the .py files of root plugin directory only,
    # other .py files should be considerd for ymake configuration hash.
    first_level = True
    for root, subdirs, files in os.walk(s, topdown=True):
        if b'tests' in subdirs:
            # 'tests' directories should not be taken into account.
            subdirs.remove(b'tests')
        for x in sorted(files):
            if x.endswith(b'.py') and (x[0] not in b'~#.'):
                RegisterPluginFilename(os.path.join(root, x))
                try:
                    if first_level and not x.startswith(b'_'):
                        mod = __import__(six.ensure_str(x[:-3]))
                        try:
                            mod.init()
                        except AttributeError:
                            pass
                        for y in dir(mod):
                            if y.startswith('on'):
                                func = getattr(mod, y)
                                ptr = <PyObject *>func
                                RegisterMacro(six.ensure_binary(y[2:].upper()), ptr)
                except ImportError as e:
                    msg = 'ImportError: {0}. sys.path: {1}.'.format(str(e), sys.path)
                    OnPluginLoadFail(os.path.join(root, x), msg)
                    pass
        first_level = False
