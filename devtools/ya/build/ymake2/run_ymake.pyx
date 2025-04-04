from libcpp.utility cimport move as std_move

from util.generic.hash cimport THashMap
from util.generic.list cimport TList
from util.generic.ptr cimport TAtomicSharedPtr
from util.generic.string cimport TString, TStringBuf
from devtools.ya.build.ccgraph.cpp_string_wrapper cimport CppStringWrapper

import logging
import six

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


def run(binary, args, env, stderr_line_reader, raw_cpp_stdout=False, stdin_line_provider=None):
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
