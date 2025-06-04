from libcpp.utility cimport move as std_move

from util.generic.hash cimport THashMap
from util.generic.list cimport TList
from util.generic.ptr cimport TAtomicSharedPtr
from util.generic.string cimport TString, TStringBuf
from util.generic.vector cimport TVector
from devtools.ya.build.ccgraph.cpp_string_wrapper cimport CppStringWrapper
from cpython.ref cimport PyObject

import cython
import logging
import six
import threading
import time

import exts.yjson as json
import exts.strings


logger = logging.getLogger(__name__)

cdef extern from "devtools/ya/build/ymake2/run_ymake.h":
    cdef cppclass TRunYMakeResult:
        int ExitCode
        TString Stdout
        TString Stderr

    ctypedef TAtomicSharedPtr[TRunYMakeResult] TRunYMakeResultPtr

    TRunYMakeResultPtr RunYMake(
        TStringBuf binary,
        const TList[TString]& args,
        const THashMap[TString, TString]& env,
        object stderrLineReader,
        object stdinLineProvider
    ) nogil except +

    cdef cppclass TRunYmakeParams:
        TString Binary
        TList[TString] Args
        THashMap[TString, TString] Env
        PyObject* StderrLineReader
        PyObject* StdinLineProvider

    ctypedef TAtomicSharedPtr[THashMap[int, TRunYMakeResultPtr]] TRunYmakeMulticonfigResultPtr

    TRunYmakeMulticonfigResultPtr RunYMakeMulticonfig(
        const TList[TRunYmakeParams]& params
    ) nogil except +

arg_collection = dict()
result_ready_event = threading.Event()
cdef THashMap[int, TRunYMakeResultPtr] results

def run(binary, args, env, stderr_line_reader, raw_cpp_stdout=False, stdin_line_provider=None, multiconfig=False, order=None):
    cdef TString binary_c = six.ensure_binary(binary)
    cdef TList[TString] args_c
    cdef THashMap[TString, TString] env_c
    cdef TRunYMakeResultPtr res
    cdef CppStringWrapper wrapped_output

    unicode_env = exts.strings.unicodize_deep(env, exts.strings.guess_default_encoding())
    logger.debug("run: '{0} {1}' with env:\n{2}".format(
        binary,
        ' '.join(args),
        json.dumps(unicode_env or {}, indent=4)
    ))

    for arg in args:
        args_c.push_back(six.ensure_binary(arg))
    if env:
        for k, v in env.items():
            env_c[six.ensure_binary(k)] = six.ensure_binary(v)

    if multiconfig:
        arg_collection[order] = {
            'binary': binary,
            'args': args,
            'env': env,
            'stderr_line_reader': stderr_line_reader,
            'stdin_line_provider': stdin_line_provider,
        }
        result_ready_event.wait()
        res = results[order]
        # it's important to reset global objects since some handlers run this function multiple times
        results.erase(results.find(order))
    else:
        with nogil:
            res = RunYMake(binary_c, args_c, env_c, stderr_line_reader, stdin_line_provider)

    output = None
    if raw_cpp_stdout:
        wrapped_output = CppStringWrapper()
        wrapped_output.output = std_move(res.Get().Stdout)
        output = wrapped_output
    else:
        output = six.ensure_str(res.Get().Stdout)

    logger.debug("run '%s %s' finished", binary, ' '.join(args))

    return res.Get().ExitCode, output, six.ensure_str(res.Get().Stderr)


def run_scheduled(count):
    cdef TList[TRunYmakeParams] params
    cdef TRunYmakeParams param
    cdef TRunYmakeMulticonfigResultPtr results_c

    while len(arg_collection) < count:
        time.sleep(0.1)
    for _, v in sorted(arg_collection.items()):
        param.Binary = six.ensure_binary(v['binary'])
        param.Args.clear()
        for arg in v['args']:
            param.Args.push_back(six.ensure_binary(arg))
        # TODO: check env is used at all
        # param.Env.clear()
        # for k, v in v['env'].items():
        #     param.Env[six.ensure_binary(k)] = six.ensure_binary(v)
        param.StderrLineReader = <PyObject*>v['stderr_line_reader']
        param.StdinLineProvider = <PyObject*>v['stdin_line_provider']
        params.push_back(param)
    with nogil:
        results_c = RunYMakeMulticonfig(params)
    for k in arg_collection:
        results[k] = results_c.Get().at(k)
    result_ready_event.set()
    # it's important to reset global objects since some handlers run this function multiple times
    result_ready_event.clear()
    arg_collection.clear()
