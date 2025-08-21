import os
import six
import logging
import traceback
import difflib
import typing  # noqa: F401

from devtools.ya.test import const
from devtools.ya.test.common import ytest_common_tools
from yatest_lib import external
import diff_match_patch
import exts.func
import exts.hashing
import exts.os2
import exts.tmp
import exts.windows
import devtools.ya.test.common
import devtools.ya.test.system.process
import yalibrary.display
import yalibrary.formatter
import yalibrary.tools

MAX_PREVIEW_LENGTH = 64
SANDBOX_RESOURCE_FILE_NAME = "canonical.data"

logger = logging.getLogger(__name__)


class ComparerInternalException(Exception):
    pass


class Crumbs(object):
    def __init__(self, crumbs=""):
        self._crumbs = crumbs

    def __str__(self):
        return self._crumbs

    def __add__(self, crumb):
        if isinstance(crumb, six.string_types):
            crumb = "'{}'".format(crumb)
        if self._crumbs:
            crumbs = "{}[{}]".format(self._crumbs, crumb)
        else:
            crumbs = "test_result[{}]".format(crumb)
        return Crumbs(crumbs)


class TestResultCrumbs(Crumbs):
    pass


class DirCrumbs(Crumbs):
    def __init__(self, dir_name):
        super(DirCrumbs, self).__init__()
        self._crumbs = dir_name

    def __add__(self, crumb):
        return DirCrumbs(os.path.join(self._crumbs, crumb))


class ResultsComparer(object):
    def __init__(
        self,
        test_name,
        sandbox_storage,
        canonical_dir,
        test_canonical_dir,
        output_dir,
        max_file_diff_length=1024,
        diff_generating_timeout=20,
        max_len_for_difflib_method=2048,
        max_file_diff_length_deviation=0.1,
        mds_storage=None,
    ):
        """
        :param test_name: name of the test to compare results
        :param sandbox_storage: SandboxStorage instance
        :param canonical_dir: canondata suite dir
        :param test_canonical_dir: dir for canonical test files
        :param output_dir: dir where big file diffs will be placed
        :param max_file_diff_length: maximum file diff length to be inlined, otherwise will be extracted to output_dir
        :param diff_generating_timeout: timeout for diff generating
        :param max_file_diff_length_deviation: maximum deviation from max_file_diff_length for not extracting diff to a separate file (if 90% is shown why extract the other 10%)
        """
        self._test_name = test_name
        self._sandbox_storage = sandbox_storage
        self._test_canonical_dir = test_canonical_dir
        self._canonical_dir = canonical_dir
        self._output_dir = output_dir
        self._diff_timeout = diff_generating_timeout
        self._max_file_diff_length = max_file_diff_length
        self._max_len_for_difflib_method = max_len_for_difflib_method
        self._max_file_diff_length_deviation = max_file_diff_length_deviation
        self._mds_storage = mds_storage

    @property
    def test_name(self):
        return ytest_common_tools.normalize_filename(self._test_name, rstrip=True)

    def diff(self, given, expected):
        extracted = None
        diffs = self._get_diffs(given, expected, TestResultCrumbs())
        d = os.linesep.join(diffs)
        whole_diff_max_len = self._max_file_diff_length * 5  # XXX extract to parameter on demand
        if whole_diff_max_len and len(d) > whole_diff_max_len:
            extracted = self._save_diff(self.test_name + ".diff", yalibrary.display.strip_markup(d))
            d = yalibrary.formatter.truncate_middle(
                d,
                whole_diff_max_len,
                ellipsis="\n...\n",
                message="\n\nsee test diff [[path]]{}[[rst]]".format(extracted),
            )
        return d, extracted

    def _get_expected_external_path_and_checksum(self, external_expected):
        if external_expected.is_sandbox_resource:
            resource_id = external_expected.path
            res_path_parts = resource_id.split("/", 1)
            if len(res_path_parts) == 2:
                resource_id, rel_path = res_path_parts
                rel_path = os.path.normpath(rel_path)
            else:
                resource_id = res_path_parts[0]
                rel_path = SANDBOX_RESOURCE_FILE_NAME
            expected_file_path = os.path.join(
                self._sandbox_storage.get(resource_id, decompress_if_archive=True).path, rel_path
            )
            if not os.path.exists(expected_file_path):
                raise ComparerInternalException("Resource {} is not an uploaded canonical result".format(resource_id))
        elif external_expected.is_http:
            res_path_parts = external_expected.uri.split("#", 1)
            if len(res_path_parts) == 2:
                _, rel_path = res_path_parts
                rel_path = os.path.normpath(rel_path)
            else:
                rel_path = ""

            mds_key = external_expected.get_mds_key()
            resource_path = self._mds_storage.get_resource(mds_key)

            expected_file_path = os.path.join(resource_path, rel_path)
        elif os.path.exists(external_expected.path):
            expected_file_path = external_expected.path
        else:
            if not self._test_canonical_dir:
                raise ComparerInternalException(
                    "Canonical dir is not set, don't know where to search {}".format(external_expected.path)
                )
            expected_file_path = os.path.join(self._test_canonical_dir, external_expected.path)  # canonization v1
            if not os.path.exists(expected_file_path):
                # canonization v2
                expected_file_path = os.path.join(os.path.join(self._canonical_dir, external_expected.path))
            if not os.path.exists(expected_file_path):
                raise ComparerInternalException("Expected file path cannot be found: {}".format(external_expected.path))

        expected_checksum = external_expected.checksum
        if not expected_checksum:
            expected_checksum = exts.hashing.md5_path(expected_file_path, include_dir_layout=True)

        return expected_file_path, expected_checksum

    def _get_diffs(self, given, expected, crumbs):
        # type: (typing.Any, typing.Any, Crumbs) -> list[str]
        diffs = []  # type: list[str]

        if external.is_external(given):
            given = external.ExternalDataInfo(given)
        if external.is_external(expected):
            expected = external.ExternalDataInfo(expected)

        if type(given) is not type(expected):
            if isinstance(given, six.string_types) and isinstance(expected, external.ExternalDataInfo):
                given_checksum = exts.hashing.md5_value(devtools.ya.test.common.to_utf8(given))
                expected_file_path, expected_checksum = self._get_expected_external_path_and_checksum(expected)
                if given_checksum != expected_checksum:
                    with open(expected_file_path) as expected_file:
                        diff_content = self._get_text_diffs(given, expected_file.read())
                        if diff_content:
                            diffs.append(
                                self._get_diff_message("value content differs:\n{}".format(diff_content), crumbs)
                            )
                        else:
                            diffs.append(
                                self._get_diff_message(
                                    "Canonization content is same with canonized before but checksum differs.\n"
                                    "Make sure that you didn't change checksum in {}/{} manually.\n"
                                    "It may also be due to switching to directory canonization".format(
                                        const.CANON_DATA_DIR_NAME, const.CANON_RESULT_FILE_NAME
                                    ),
                                    crumbs,
                                )
                            )
            else:
                diffs.append(
                    self._get_diff_message(
                        "canonical results type differs from test results: expected {expected_type} ({expected_preview}), got {given_type} ({given_preview})".format(
                            expected_type=type(expected),
                            expected_preview=preview(expected),
                            given_type=type(given),
                            given_preview=preview(given),
                        ),
                        crumbs,
                    )
                )
        else:
            if type(expected) in [int, float, bool, str, six.text_type]:
                if given != expected:
                    if isinstance(expected, six.string_types):
                        diff = self._get_text_diffs(given, expected)
                    else:
                        diff = "value {} differs from canonical {}".format(preview(given), preview(expected))
                    diffs.append(self._get_diff_message(diff, crumbs))

            if type(expected) in [list, tuple]:
                for i, expected_list_value in enumerate(expected):
                    if i <= len(given) - 1:
                        given_list_value = given[i]
                        diffs.extend(self._get_diffs(given_list_value, expected_list_value, crumbs + i))
                    else:
                        diffs.append(
                            self._get_diff_message(
                                "value {} is missing".format(preview(expected_list_value)), crumbs + i
                            )
                        )
                if len(given) > len(expected):
                    diffs.append(
                        ", ".join(
                            [
                                self._get_diff_message("extra value {}".format(preview(given[i])), crumbs + i)
                                for i in range(len(expected), len(given))
                            ]
                        )
                    )

            if isinstance(expected, external.ExternalDataInfo):
                if isinstance(given, external.ExternalDataInfo):
                    given_path, given_checksum = self._get_expected_external_path_and_checksum(given)
                    expected_file_path, expected_checksum = self._get_expected_external_path_and_checksum(expected)
                    if expected_checksum != given_checksum:
                        path_diff = self._get_path_diff(
                            given_path,
                            expected_file_path,
                            given.get("diff_tool"),
                            given.get("diff_file_name"),
                            given.get("diff_tool_timeout"),
                        )
                        # It's fine if diff_tool founds no diff - it may suppress some changes valid for test owner
                        if path_diff:
                            diffs.append(self._get_diff_message(path_diff, crumbs))
                else:
                    diffs.extend(self._get_diffs(given, expected, crumbs + i))

            if type(expected) is dict:
                for key, expected_dict_value in six.iteritems(expected):
                    if key in given:
                        given_dict_value = given[key]
                        diffs.extend(
                            self._get_diff_message(self._get_diffs(given_dict_value, expected_dict_value, crumbs + key))
                        )
                    else:
                        diffs.append(
                            self._get_diff_message(
                                "value {} is missing".format(preview(expected_dict_value)),
                                crumbs + key,
                            )
                        )

                extra_keys = []
                for key in given:
                    if key not in expected:
                        extra_keys.append(key)
                if extra_keys:
                    diffs.append(
                        ", ".join(
                            [
                                self._get_diff_message(
                                    "extra key with value {}".format(preview(given[key])), crumbs + key
                                )
                                for key in extra_keys
                            ]
                        )
                    )
        return list(map(six.ensure_str, diffs))

    def _get_diff_message(self, message, crumbs=None):
        if crumbs and str(crumbs):
            message = "{}: {}".format(crumbs, message)
        return message

    def _save_diff(self, filename, diff):
        output_dir = os.path.join(self._output_dir, self.test_name)
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        filename = devtools.ya.test.common.get_unique_file_path(output_dir, filename)
        diff = six.ensure_str(diff)
        with open(filename, "w") as afile:
            afile.write(diff)
        return filename

    def _get_path_diff(self, given, expected, custom_diff_tool=None, diff_file_name=None, diff_diff_tool_timeout=None):
        def path_type(path):
            if os.path.isdir(path):
                return "dir"
            return "file"

        if os.path.isfile(given) and os.path.isfile(expected):
            diff = self._get_file_diff(given, expected, custom_diff_tool, diff_file_name, diff_diff_tool_timeout)
            if diff:
                diff = "files content differs:\n{}".format(diff)
            return diff
        if os.path.isdir(given) and os.path.isdir(expected):
            if custom_diff_tool:
                diff = self._get_file_diff(given, expected, custom_diff_tool, diff_file_name, diff_diff_tool_timeout)
            else:
                diff = self._get_dirs_diff(given, expected)

            if diff:
                diff = "dirs content differs:\n{}".format(diff)
            return diff
        raise Exception(
            "Cannot compare: given {} {} with expected {} {}".format(
                path_type(given), given, path_type(expected), expected
            )
        )

    def _get_file_diff(self, given, expected, custom_diff_tool=None, diff_file_name=None, diff_diff_tool_timeout=None):
        try:
            if custom_diff_tool:
                if type(custom_diff_tool) is not list:
                    custom_diff_tool = [custom_diff_tool]
                binary_path = custom_diff_tool[0]
                if os.path.isdir(binary_path):
                    raise Exception("Specified custom diff-tool is a directory, not executable: {}".format(binary_path))
                if not os.path.isfile(binary_path):
                    raise Exception("Custom diff-tool is missing (forgot DEPENDS(<tool-path>?): {}".format(binary_path))
                if not os.access(binary_path, os.X_OK):
                    raise Exception("Invalid custom diff-tool, not executable: {}".format(binary_path))
            diff_tool = custom_diff_tool if custom_diff_tool else self._get_diff_tool()
            if diff_tool:
                diff = self._get_file_diff_via_diff(diff_tool, given, expected, diff_diff_tool_timeout)
            else:
                diff = self._get_file_diff_diff_match_patch(given, expected)

            if (
                diff
                and self._max_file_diff_length
                and len(diff) > self._max_file_diff_length + len(diff) * self._max_file_diff_length_deviation
            ):
                filename = self._save_diff(diff_file_name or os.path.basename(given) + ".diff", diff)
                diff = preview(diff, self._max_file_diff_length)
                diff += "\nsee full diff [[path]]{}[[rst]][[bad]]".format(os.path.abspath(filename))
            return diff
        except Exception as e:
            logger.error("Error while calculating file diff: %s", traceback.format_exc())
            return "Error while calculating file diff: {}".format(e)

    def _get_system_diff_tool_path(self):
        return "/usr/bin/diff"

    @exts.func.memoize()
    def _get_diff_tool(self):
        system_diff = self._get_system_diff_tool_path()
        if os.path.exists(system_diff):
            return [system_diff, "-U1"]
        fast_diff = os.path.join(yalibrary.tools.tool("fast_diff"), "fast_diff")
        if exts.windows.on_win():
            fast_diff += ".exe"
        if (
            os.path.exists(fast_diff)
            and devtools.ya.test.system.process.execute([fast_diff, "--help"], check_exit_code=False).exit_code == 1
        ):
            return [fast_diff]
        return None

    def _get_dirs_diff(self, given, expected):
        def path_to_file_dict(path):
            tree = {}
            for dir_path, dirs, files in exts.os2.fastwalk(path):
                for f in files:
                    file_path = os.path.join(dir_path, f)
                    rel_file_path = os.path.relpath(file_path, path)
                    tree[rel_file_path] = external.ExternalDataInfo.serialize_file(file_path)
            return tree

        return os.linesep.join(
            self._get_diffs(path_to_file_dict(given), path_to_file_dict(expected), DirCrumbs(os.path.basename(given)))
        )

    def _get_file_diff_via_diff(self, diff_tool_path, given, expected, diff_diff_tool_timeout):
        try:
            res = devtools.ya.test.system.process.execute(
                diff_tool_path + [expected, given],
                check_exit_code=False,
                timeout=diff_diff_tool_timeout or self._diff_timeout,
                text=True,
            )
            if res.exit_code == 1 and res.std_out:
                return res.std_out
            if res.exit_code == 0:
                return ""
            raise Exception(
                "'{}' has finished unexpectedly with rc = {}\nstdout:\n{}\nstderr:\n{}".format(
                    " ".join(diff_tool_path), res.exit_code, res.std_out, res.std_err
                )
            )
        except Exception as e:
            logger.error("Cannot calculate diff: %s", traceback.format_exc())
            return str(e)

    def _get_file_diff_diff_match_patch(self, given, expected):
        with open(given) as given_file:
            with open(expected) as expected_file:
                expected_file_content = expected_file.read()
                given_file_content = given_file.read()
        differ = diff_match_patch.diff_match_patch()
        differ.Diff_Timeout = self._diff_timeout
        diffs = differ.diff_main(expected_file_content, given_file_content)
        res = ""
        for op, diff in diffs:
            if op == differ.DIFF_DELETE:
                res += "-"
            elif op == differ.DIFF_INSERT:
                res += "+"
            res += diff
        return res

    def _get_text_diffs(self, given, expected):
        if any([len(text) > self._max_len_for_difflib_method for text in [given, expected]]):
            with exts.tmp.temp_file() as given_file_path, exts.tmp.temp_file() as expected_file_path:
                with open(given_file_path, "w") as given_fd, open(expected_file_path, "w") as expected_fd:
                    given_fd.write(six.ensure_str(given))
                    expected_fd.write(six.ensure_str(expected))
                return self._get_file_diff(given_file_path, expected_file_path)
        differ = difflib.Differ()
        return "\n".join(differ.compare(expected.splitlines(), given.splitlines()))


def preview(value, length=MAX_PREVIEW_LENGTH):
    # type: (..., int) -> str
    ellipsis = "..."
    res = "{}".format(value)
    if len(res) > length:
        res = res[: length - len(ellipsis)] + ellipsis
    if isinstance(value, six.string_types):
        return "'{}'".format(res)
    return res


class _MonkeyConfig(object):
    def getoption(self, option):
        return option == "verbose"
