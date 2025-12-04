# coding: utf-8

import os
import re
import sys
import math
import functools

import six
import devtools.ya.test.const
from devtools.ya.test.const import Status
import yatest.common as yac

_YMAKE_PYTHON3_PATTERN = None


def set_python_pattern(python3_pattern):
    global _YMAKE_PYTHON3_PATTERN
    _YMAKE_PYTHON3_PATTERN = python3_pattern


def get_test_suite_work_dir(
    build_root,
    project_path,
    test_name,
    retry=None,
    split_count=1,
    split_index=0,
    target_platform_descriptor=None,
    split_file=None,
    multi_target_platform_run=False,
    remove_tos=False,
):
    if build_root:
        paths = [build_root]
    else:
        paths = []
    paths += [project_path, "test-results", test_name]
    # Don't store test results into additional directory level if only one test target platform was requested
    if target_platform_descriptor and multi_target_platform_run:
        paths.append(target_platform_descriptor)
    subpaths = []
    if retry is not None:
        if remove_tos:
            subpaths += ["run{}".format(retry)]
        else:
            subpaths += ["{}".format(retry)]
    if split_file:
        subpaths.append(os.path.splitext(split_file)[0])
    if split_count > 1:
        subpaths.append("chunk{}".format(split_index))
    if subpaths:
        if remove_tos:
            paths += subpaths
        else:
            paths += [devtools.ya.test.const.TESTING_OUT_DIR_NAME] + subpaths
    return to_utf8(os.path.join(*paths))


def to_utf8(value):
    """
    Converts value to string encoded into utf-8
    :param value:
    :return:
    """
    if not isinstance(value, six.string_types):
        value = six.text_type(value)

    return normalize_utf8(value)


def strings_to_utf8(data):
    if isinstance(data, dict):
        return {to_utf8(key): strings_to_utf8(value) for key, value in data.items()}
    elif isinstance(data, list):
        return [strings_to_utf8(element) for element in data]
    elif isinstance(data, tuple):
        return tuple((strings_to_utf8(element) for element in data))
    elif isinstance(data, six.text_type):
        return to_utf8(data)
    else:
        return data


class SubtestInfo(object):
    skipped_prefix = '[SKIPPED] '

    @classmethod
    def from_str(cls, s):
        # XXX remove when junit and pytest return json formatted test lists
        if s.startswith(SubtestInfo.skipped_prefix):
            s = s[len(SubtestInfo.skipped_prefix) :]
            skipped = True

        else:
            skipped = False

        return SubtestInfo(*s.rsplit(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, 1), skipped=skipped)

    @classmethod
    def from_json(cls, d):
        return SubtestInfo(d["test"], d.get("subtest", ""), skipped=d.get("skipped", False), tags=d.get("tags", []))

    def __init__(self, test, subtest="", skipped=False, **kwargs):
        self.test = test
        self.subtest = normalize_utf8(subtest)
        self.skipped = skipped
        for key, value in kwargs.items():
            setattr(self, key, strings_to_utf8(value))

    def __str__(self):
        s = ''

        if self.skipped:
            s += SubtestInfo.skipped_prefix

        return s + devtools.ya.test.const.TEST_SUBTEST_SEPARATOR.join([self.test, self.subtest])

    def __repr__(self):
        return str(self)

    def to_json(self):
        return {
            "test": self.test,
            "subtest": self.subtest,
            "skipped": self.skipped,
            "tags": getattr(self, "tags", []),
        }


def get_formatted_statuses(func, pattern):
    result = []
    for status, marker in [
        (Status.GOOD, 'good'),
        (Status.FAIL, 'bad'),
        (Status.NOT_LAUNCHED, 'bad'),
        (Status.XFAIL, 'warn'),
        (Status.XPASS, 'warn'),
        (Status.TIMEOUT, 'bad'),
        (Status.SKIPPED, 'unimp'),
        (Status.MISSING, 'alt1'),
        (Status.CRASHED, 'alt2'),
        (Status.FLAKY, 'alt3'),
        (Status.CANON_DIFF, 'bad'),
        (Status.INTERNAL, 'bad'),
        (Status.DESELECTED, 'unimp'),
    ]:
        entry_count = func(status)
        if entry_count:
            result.append(pattern.format(marker=marker, count=entry_count, status=Status.TO_STR[status].upper()))
    return result


class NoMd5FileException(Exception):
    pass


# TODO: extract color theme logic from ya
COLOR_THEME = {
    'test_name': 'light-blue',
    'test_project_path': 'dark-blue',
    'test_dir_desc': 'dark-magenta',
    'test_binary_path': 'light-gray',
}


def lazy(func):
    mem = {}

    @functools.wraps(func)
    def wrapper():
        if "results" not in mem:
            mem["results"] = func()
        return mem["results"]

    return wrapper


@lazy
def _get_mtab():
    if os.path.exists("/etc/mtab"):
        with open("/etc/mtab") as afile:
            data = afile.read()
        return [line.split(" ") for line in data.split("\n") if line]
    return []


def get_max_filename_length(dirname):
    """
    Return maximum filename length for the filesystem
    :return:
    """
    if sys.platform.startswith("linux"):
        # Linux user's may work on mounted ecryptfs filesystem
        # which has filename length limitations
        for entry in _get_mtab():
            mounted_dir, filesystem = entry[1], entry[2]
            # http://unix.stackexchange.com/questions/32795/what-is-the-maximum-allowed-filename-and-folder-size-with-ecryptfs
            if filesystem == "ecryptfs" and dirname and dirname.startswith(mounted_dir):
                return 140
    # default maximum filename length for most filesystems
    return 255


def get_unique_file_path(dir_path, filename, create_file=True):
    """
    Get unique filename in dir with proper filename length, using given filename
    :param dir_path: path to dir
    :param filename: original filename
    :param create_file: whether create file or directory
    :return: unique filename
    """
    max_suffix = 10000
    # + 1 symbol for dot before suffix
    tail_length = int(round(math.log(max_suffix, 10))) + 1
    # truncate filename length in accordance with filesystem limitations
    filename, extension = os.path.splitext(filename)
    # XXX
    filename = filename.encode("utf-8")
    if sys.platform.startswith("win"):
        # Trying to fit into MAX_PATH if it's possible.
        # Remove after DEVTOOLS-1646
        max_path = 260
        filename_len = len(dir_path) + len(extension) + tail_length + len(os.sep)
        if filename_len < max_path:
            filename = filename[: max_path - filename_len]

    filename = filename[: get_max_filename_length(dir_path) - tail_length - len(extension)]
    filename = filename.decode("utf-8", errors='ignore') + extension
    filename = normalize_utf8(filename)

    return yac.get_unique_file_path(dir_path, filename, create_file=create_file, max_suffix=max_suffix)


def get_python_cmd(opts=None, suite=None):
    if opts and getattr(opts, 'flags', {}).get("USE_ARCADIA_PYTHON") == "no":
        return ["python3"]
    if suite and not suite._use_arcadia_python:
        return ["python3"]
    assert _YMAKE_PYTHON3_PATTERN is not None, "Seems you are not call set_python_pattern() function"
    return ["$({})/bin/python3".format(_YMAKE_PYTHON3_PATTERN)]


def normalize_filename(filename, rstrip=False):
    """
    Replace invalid for file names characters with string equivalents
    :param some_string: string to be converted to a valid file name
    :return: valid file name
    """
    not_allowed_pattern = r"[\[\]\/:*?\"\'<>|+\0\\\t\n\r\x0b\x0c ]"
    filename = re.sub(not_allowed_pattern, ".", filename)
    if rstrip:
        filename = filename.rstrip(".")
    return re.sub(r"\.{2,}", ".", filename)


def normalize_utf8(value):
    """
    For python3:
    str -> str / str -> str  # Base string is unicode, all will be ok even if something strange into string
    bytes -> str / str -> str  # decode bytes to str with ignore errors

    For python2:
    str (bytes) -> unicode / unicode -> str  # Base string is bytes string, so we convert data and return original string
    unicode -> unicode / unicode -> str  # Return base string
    """
    uvalue = six.ensure_text(value, encoding='utf-8', errors='ignore')  # ? -> unicode
    return six.ensure_str(uvalue, encoding='utf-8', errors='ignore')  # unicode -> str


def get_test_log_file_path(output_dir, test_name, extension="log"):
    """
    get test log file path, platform dependant
    :param output_dir: dir where log file should be placed
    :param test_name: test name
    :return: test log file name
    """
    if os.name == "nt":
        # don't add class name to the log's filename
        # to reduce it's length on windows
        filename = test_name
    else:
        filename = test_name.replace(devtools.ya.test.const.TEST_SUBTEST_SEPARATOR, ".")
    if not filename:
        filename = "test"
    filename += "." + extension
    filename = normalize_filename(filename)
    return get_unique_file_path(output_dir, filename)


class TestsMerger(object):
    def __init__(self, merger_node):
        self.uid = merger_node["uid"]
        self.deps = merger_node["deps"]
        self.outputs = merger_node["outputs"]

    def result_path(self, build_root=None):
        result_paths = [output for output in self.outputs if 'report_prototype.json' in output]

        if len(result_paths) == 0:
            raise Exception('Report prototype path not found in outputs (got {0})'.format(str(self.outputs)))

        result_path = result_paths[0]
        if build_root is not None:
            result_path = result_path.replace('$(BUILD_ROOT)', build_root)

        return result_path
