import os
import re
import sys
import base64
import contextlib
import shlex

import pytest
import six
import yatest.common

import yalibrary.platform_matcher

ENV_MARKER = "ENV"
NAME_MARKER = "NAME"
STD_IN_MARKER = "STDIN"
STD_OUT_MARKER = "STDOUT"
STD_ERR_MARKER = "STDERR"
CANONIZE_PATH = "CANONIZE"
CANONIZE_FILE_LOCALLY = "CANONIZE_LOCALLY"
CANONIZE_DIR = "CANONIZE_DIR"
CANONIZE_DIR_LOCALLY = "CANONIZE_DIR_LOCALLY"
CWD_DIR = "CWD"
DIFF_TOOL_MARKER = "DIFF_TOOL"
DIFF_TOOL_TIMEOUT_MARKER = "DIFF_TOOL_TIMEOUT"
PYTHON_BIN = "${PYTHON_BIN}"
TEST_NAMES = []

REPLACEMENTS = {
    "${ARCADIA_BUILD_ROOT}": yatest.common.build_path(),
    "${ARCADIA_ROOT}": yatest.common.source_path(),
    "${TEST_OUT_ROOT}": yatest.common.output_path(),
    "${TEST_SOURCE_ROOT}": yatest.common.test_source_path(),
    "${TEST_WORK_ROOT}": yatest.common.work_path(),
    # unique for every test (test_output_path will create directory itself)
    "${TEST_CASE_ROOT}": yatest.common.test_output_path,
    PYTHON_BIN: os.environ["YA_PYTHON_BIN"],
}


class OpenFileAction(object):
    def __init__(self, path, mode):
        self._path = path
        self._mode = mode

    def __call__(self, *args, **kwargs):
        return open(self._path, mode=self._mode)


@contextlib.contextmanager
def null_file_object():
    yield


def get_commands():
    commands = yatest.common.get_param("commands")
    if commands:
        commands = base64.b64decode(commands)
        commands = six.ensure_str(commands)
    else:
        raise Exception("Please, add one or more RUN sections to EXECTEST")
    return [_f for _f in [c.strip() for c in commands.split('\n')] if _f]


def strip_markers(command):
    found_markers = {CANONIZE_PATH: []}
    command = shlex.split(command)
    res_command = []
    markers_for_canonize = (CANONIZE_PATH, CANONIZE_FILE_LOCALLY, CANONIZE_DIR, CANONIZE_DIR_LOCALLY)
    markers_with_values = (
        NAME_MARKER,
        STD_IN_MARKER,
        STD_OUT_MARKER,
        STD_ERR_MARKER,
        DIFF_TOOL_MARKER,
        DIFF_TOOL_TIMEOUT_MARKER,
        CWD_DIR,
    )
    markers_with_list_values = (ENV_MARKER,)

    while command:
        part = command.pop(0)
        if part in markers_with_values + markers_for_canonize + markers_with_list_values:
            if part in markers_for_canonize:
                marker_val = found_markers[CANONIZE_PATH]
                marker_val.append(
                    (
                        command.pop(0),
                        part in (CANONIZE_FILE_LOCALLY, CANONIZE_DIR_LOCALLY),
                        part in (CANONIZE_DIR, CANONIZE_DIR_LOCALLY),
                    )
                )
                part = CANONIZE_PATH
            elif part in markers_with_list_values:
                current = found_markers.get(part, [])
                marker_val = current + [command.pop(0)]
            elif part in markers_with_values:
                marker_val = command.pop(0)
            else:
                marker_val = True
            found_markers[part] = marker_val
        else:
            res_command.append(part)
    return res_command, found_markers


def get_test_name(command):
    command, markers = strip_markers(command)
    test_desired_name = markers.get(NAME_MARKER, os.path.basename(command[0]))
    if test_desired_name == PYTHON_BIN:
        if len(command) > 1:
            test_desired_name = command[1]
        else:
            test_desired_name = "python_bin"
    test_name = test_desired_name
    i = 0
    while test_name in TEST_NAMES:
        test_name = test_desired_name + str(i)
        i += 1
    TEST_NAMES.append(test_name)
    return test_name


def make_replace(command):
    match = re.search(r'\${[\w_]+}', command)
    if match:
        pat = match.group(0)
        val = REPLACEMENTS[pat]
        if hasattr(val, '__call__'):
            val = val()
        val = val.replace("\\", "/")
        command = command.replace(pat, val)
        return make_replace(command)
    return command


def verify_path_is_inside_sandbox(path):
    path = os.path.abspath(path)
    for valid_prefix in (
        yatest.common.source_path(),
        yatest.common.build_path(),
        yatest.common.data_path(),
        os.getcwd(),
    ):
        if path.startswith(valid_prefix):
            return
    raise Exception("Invalid path outside of the sandbox: {}".format(path))


@pytest.mark.parametrize("command", get_commands(), ids=get_test_name)
def run(command):
    command = make_replace(command)

    command, markers = strip_markers(command)
    ios = {}
    test_cwd = None

    if CWD_DIR in markers:
        test_cwd = markers[CWD_DIR]
        verify_path_is_inside_sandbox(test_cwd)

        def normpath(p):
            if os.path.isabs(p):
                return p
            return "{}/{}".format(test_cwd, p)

        for marker in (
            STD_IN_MARKER,
            STD_OUT_MARKER,
            STD_ERR_MARKER,
            CANONIZE_PATH,
            CANONIZE_FILE_LOCALLY,
            CANONIZE_DIR,
            CANONIZE_DIR_LOCALLY,
        ):
            if marker not in markers:
                continue

            if isinstance(markers[marker], list):
                markers[marker] = [(normpath(p), a, d) for p, a, d in markers[marker]]
            else:
                markers[marker] = normpath(markers[marker])

    for marker in (STD_IN_MARKER, STD_OUT_MARKER, STD_ERR_MARKER):
        if marker in markers:
            verify_path_is_inside_sandbox(markers[marker])
            ios[marker] = OpenFileAction(markers[marker], "rb" if marker == STD_IN_MARKER else "wb")
        else:
            ios[marker] = lambda: null_file_object()

    env = os.environ.copy()
    for env_marker in markers.get(ENV_MARKER, []):
        if '=' not in env_marker:
            raise Exception("ENV '{}' does not follow pattern <NAME>=<VALUE>".format(env_marker))
        name, val = env_marker.split("=", 1)
        env[name] = val

    ram_drive_path = yatest.common.get_param("ram_drive_path")
    if ram_drive_path:
        env["YATEST_RAM_DRIVE_PATH"] = ram_drive_path

    python_library_path = os.environ.get("YA_PYTHON_LIB")
    if python_library_path:
        host_os = yalibrary.platform_matcher.current_os()
        if host_os == "LINUX":
            env["LD_LIBRARY_PATH"] = python_library_path
        elif host_os == "DARWIN":
            env["DYLD_FRAMEWORK_PATH"] = python_library_path

    diff_tool = markers.get(DIFF_TOOL_MARKER)
    if diff_tool:
        diff_tool = diff_tool.split(" ")
        diff_tool[0] = yatest.common.binary_path(diff_tool[0])
    diff_tool_timeout = None
    if markers.get(DIFF_TOOL_TIMEOUT_MARKER):
        diff_tool_timeout = int(markers.get(DIFF_TOOL_TIMEOUT_MARKER))
    with ios[STD_IN_MARKER]() as stdin, ios[STD_OUT_MARKER]() as stdout, ios[STD_ERR_MARKER]() as stderr:
        res = yatest.common.execute(command, stdin=stdin, stdout=stdout, stderr=stderr, env=env, cwd=test_cwd)

        if STD_ERR_MARKER not in markers:
            sys.stderr.write(six.ensure_str(res.std_err, errors='ignore'))

        canonical_callbacks = {
            # Always return list for directory canonization to keep structure in
            # canon description when there will be more than one file
            True: lambda *a, **kv: [yatest.common.canonical_dir(*a, **kv)],
            False: yatest.common.canonical_file,
        }

        if markers[CANONIZE_PATH]:
            list(map(verify_path_is_inside_sandbox, [path for path, _, _ in markers[CANONIZE_PATH]]))

            if len(markers[CANONIZE_PATH]) == 1:
                path, local, is_dir = markers[CANONIZE_PATH][0]
                return canonical_callbacks[is_dir](
                    path, local=local, diff_tool=diff_tool, diff_tool_timeout=diff_tool_timeout
                )

            return [
                canonical_callbacks[d](f, local=a, diff_tool=diff_tool, diff_tool_timeout=diff_tool_timeout)
                for f, a, d in markers[CANONIZE_PATH]
            ]
