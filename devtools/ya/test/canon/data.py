# coding: utf-8

import os
import re
import json
import copy
import shutil
import logging

import six
import traceback

import devtools.ya.test.common as common
from . import compare as test_canon_compare
import devtools.ya.test.util.tools as test_tools
import devtools.ya.test.const as test_const
import devtools.ya.test.system.process
import exts.fs
import exts.func
import exts.uniq_id
import exts.hashing
import yalibrary.tools as tools
from exts import tmp
import yalibrary.upload.consts
from yalibrary.display import strip_markup
from yatest_lib import external
from devtools.ya.test import const

try:
    from yalibrary.vcs import detect
except ImportError:
    from standalone import detect

import app_config

if app_config.in_house:
    import yalibrary.svn as svn
    import devtools.ya.test.canon.upload as upload


MAX_DEFAULT_FILE_SIZE = 150 * 1024
DEFAULT_FILE_GROUP_SIZE = 200 * 1024 * 1024
SANDBOX_CANONICAL_RESOURCE_TYPE = "CANONICAL_TEST_DATA"

yatest_logger = logging.getLogger("ya.test")


class CanonizationException(Exception):
    pass


class CanonicalDataMissingError(CanonizationException):
    pass


class CanonicalResultVerificationException(CanonizationException):
    def __init__(self, msg, path):
        super(CanonicalResultVerificationException, self).__init__(msg)
        self.diff_path = path


class DiffTestResultSanityCheckException(Exception):
    pass


class VCS(object):
    name = None

    def active(self):
        raise NotImplementedError()

    def apply(self, test_canonical_dir):
        raise NotImplementedError()


class SvnRepo(VCS):
    try:
        SVN_CHOOSE_POLICY = svn.SvnChoosePolicy.prefer_local_svn_but_not_16()
    except NameError:
        SVN_CHOOSE_POLICY = None

    name = "svn"

    def __init__(self, arcadia_root):
        self._active = svn.is_under_control(arcadia_root, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)

    def active(self):
        return self._active

    def apply(self, test_canonical_dir):
        test_suite_canonical_dir = os.path.abspath(os.path.join(test_canonical_dir, ".."))
        test_suite_dir = os.path.abspath(os.path.join(test_suite_canonical_dir, ".."))
        if svn.is_under_control(
            test_suite_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY
        ) and not svn.is_under_control(test_suite_canonical_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY):
            # if test suite canonical dir is not under svn - add it (only it)
            yatest_logger.debug("Adding to svn test suite canonical dir %s", test_suite_canonical_dir)
            svn.svn_add(test_suite_canonical_dir, depth='empty', svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)
        if (
            os.path.exists(test_canonical_dir)
            and svn.is_under_control(test_suite_canonical_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)
            and not svn.is_under_control(test_canonical_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)
        ):
            # add test canonical dir
            svn.svn_add(test_canonical_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)

        # check and fix missed and new files
        svn_st = svn.svn_st(test_canonical_dir, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)
        for_svn_rm = []
        for_svn_add = []
        for path, path_st in svn_st.items():
            if path_st["item"] == "missing":
                for_svn_rm.append(path)
            if path_st["item"] == "unversioned":
                for_svn_add.append(path)
        if for_svn_rm:
            with tmp.temp_file() as rm_targets:
                with open(rm_targets, "w") as rm_targets_file:
                    rm_targets_file.write("\n".join(for_svn_rm))
                svn.svn_rm(targets=rm_targets, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)
        if for_svn_add:
            with tmp.temp_file() as add_targets:
                with open(add_targets, "w") as add_targets_file:
                    add_targets_file.write("\n".join(for_svn_add))
                svn.svn_add(targets=add_targets, svn_choose_policy=SvnRepo.SVN_CHOOSE_POLICY)


class HgRepo(VCS):
    name = "hg"

    def __init__(self, arcadia_root):
        self._active = (
            devtools.ya.test.system.process.execute(
                [tools.tool('hg'), "branch"], cwd=arcadia_root, check_exit_code=False
            ).exit_code
            == 0
        )

    def active(self):
        return self._active

    def apply(self, test_canonical_dir):
        st = devtools.ya.test.system.process.execute(
            [tools.tool('hg'), "status", test_canonical_dir, "-T", "json"], cwd=test_canonical_dir
        )
        status = json.loads(st.std_out)
        to_be_added = []
        to_be_removed = []
        for rec in status:
            path = os.path.join(test_canonical_dir, rec["path"])
            if rec["status"] == "?":
                to_be_added.append(path)
            elif rec["status"] == "!":
                to_be_removed.append(path)
        if to_be_added:
            devtools.ya.test.system.process.execute([tools.tool('hg'), "add"] + to_be_added)
        if to_be_removed:
            devtools.ya.test.system.process.execute([tools.tool('hg'), "rm"] + to_be_removed)


class ArcRepo(VCS):
    name = "arc"

    def __init__(self, arcadia_root):
        self.arcadia_root = arcadia_root
        self._active = (
            devtools.ya.test.system.process.execute(
                [tools.tool('arc'), "branch"], cwd=arcadia_root, check_exit_code=False
            ).exit_code
            == 0
        )

    def active(self):
        return self._active

    def apply(self, test_canonical_dir):
        devtools.ya.test.system.process.execute([tools.tool('arc'), "add", "."], cwd=test_canonical_dir)


class DummyRepo(VCS):
    def __init__(self, arcadia_root):
        pass

    def active(self):
        return True

    def apply(self, test_canonical_dir):
        pass


def external_diff_tool_check(new_data, old_data):
    return recursive_is_external(old_data) and recursive_external_diff_tool_check(new_data)


def recursive_is_external(data):
    if isinstance(data, dict):
        if external.is_external(data):
            return True
        else:
            return any(map(recursive_is_external, data.values()))
    elif isinstance(data, (tuple, list)):
        return any(map(recursive_is_external, data))
    else:
        return False


def recursive_external_diff_tool_check(new_data):
    if isinstance(new_data, dict):
        if external.is_external(new_data):
            return "diff_tool" in new_data
        else:
            return any(map(recursive_external_diff_tool_check, new_data.values()))
    elif isinstance(new_data, (tuple, list)):
        return any(map(recursive_external_diff_tool_check, new_data))
    else:
        return False


def _replace_backend_to_pattern(data, backend):
    data["uri"] = data["uri"].replace(backend, const.CANON_BACKEND_KEY)


def _replace_pattern_to_backend(data, backend):
    data["uri"] = data["uri"].replace(const.CANON_BACKEND_KEY, backend or const.DEFAULT_CANONIZATION_BACKEND)


def recursive_replace_uri_pattern(data, backend, replacer):
    if isinstance(data, dict):
        if "uri" in data:
            replacer(data, backend)
        else:
            for v in data.values():
                recursive_replace_uri_pattern(v, backend, replacer)
    elif isinstance(data, (tuple, list)):
        for v in data:
            recursive_replace_uri_pattern(v, backend, replacer)


class CanonicalData(object):
    def __init__(
        self,
        arc_path,
        sandbox_storage,
        resource_ttl=yalibrary.upload.consts.TTL_INF,
        resource_owner=None,
        sandbox_token=None,
        sandbox_url=yalibrary.upload.consts.DEFAULT_SANDBOX_URL,
        max_str_len=128,
        max_file_size=MAX_DEFAULT_FILE_SIZE,
        upload_transport=None,
        ssh_keys=[],
        ssh_user=None,
        max_inline_diff_size=1024,
        skynet_upload_task_state_printer=None,
        mds=False,
        mds_storage=None,
        sub_path=None,
        mds_token=None,
        oauth_token=None,
        no_src_changes=False,
        backend=None,
    ):
        self._arc_path = arc_path
        self._max_str_len = max_str_len
        self._max_file_size = max_file_size
        self._resource_owner = resource_owner
        self._sandbox_token = sandbox_token
        self._mds_token = mds_token
        self._oauth_token = oauth_token
        self._sandbox_storage = sandbox_storage
        self._mds_storage = mds_storage
        self._sandbox_url = sandbox_url
        self._upload_transport = upload_transport
        self._ssh_keys = ssh_keys
        self._ssh_user = ssh_user
        self._uploaded_resource_ttl = resource_ttl
        self._max_inline_diff_size = max_inline_diff_size
        self._skynet_upload_task_state_printer = skynet_upload_task_state_printer
        self._repo = None
        self._no_src_changes = no_src_changes
        self._mds = mds
        self._sub_path = sub_path
        self._backend = backend

    def repo(self):
        if not self._repo:
            self._repo = self._get_repo(dummy=self._no_src_changes)
        return self._repo

    def get_canonical_results(self, project_path, test_name):
        if os.path.exists(os.path.join(self.get_suite_canon_dir(project_path), const.CANON_RESULT_FILE_NAME)):
            res = self._get_canonical_results_v2(project_path, test_name)
        else:
            res = self._get_canonical_results_v1(project_path, test_name)
        recursive_replace_uri_pattern(res, self._backend, _replace_pattern_to_backend)
        return res

    def _copy_test_canonical_results(self, dest, project_path, test_name):
        res = self.get_canonical_results(project_path, test_name)
        if res is None:
            return res

        test_canon_dir = self.get_test_canon_dir(project_path, test_name)
        suite_canon_dir = self.get_suite_canon_dir(project_path)
        dest_test_canoni_dir = os.path.join(dest, os.path.basename(test_canon_dir))

        def fix_paths(value, _):
            if external.is_external(value):
                external_data = external.ExternalDataInfo(value)
                if external_data.is_file:
                    path = external_data.path
                    if os.path.exists(os.path.join(test_canon_dir, path)):
                        src = os.path.join(test_canon_dir, path)
                    elif os.path.exists(os.path.join(suite_canon_dir, path)):
                        src = os.path.join(suite_canon_dir, path)
                    else:
                        logging.warning("Found missing external file: {}".format(path))
                        return value
                    dst = os.path.join(dest_test_canoni_dir, os.path.basename(src))
                    exts.fs.ensure_dir(dest_test_canoni_dir)
                    if os.path.isdir(src):
                        exts.fs.copytree3(src, dst, dirs_exist_ok=True)
                    else:
                        exts.fs.copy_file(src, dst)
                    return dict(external.ExternalDataInfo.serialize_file(dst, external_data.checksum))
                return value
            return value

        res = external.apply(fix_paths, res)
        return copy.deepcopy(res)

    def _get_canonical_results_v1(self, project_path, test_name):
        test_description = self._get_test_full_name(project_path, test_name)
        canonical_results_path = self._try_to_get_canonical_results_path(project_path, test_name, test_description)
        if not canonical_results_path:
            # Try to find test results by legacy name
            legacy_test_name = self._get_legacy_test_name(project_path, test_name)
            if legacy_test_name:
                canonical_results_path = self._try_to_get_canonical_results_path(
                    project_path, legacy_test_name, test_description
                )
        if canonical_results_path:
            return self._get_canonical_file_for_test(canonical_results_path, test_description)
        else:
            return None

    def _get_canonical_results_v2(self, project_path, test_name):
        test_description = self._get_test_full_name(project_path, test_name)
        canonical_results_path = os.path.join(self.get_suite_canon_dir(project_path), const.CANON_RESULT_FILE_NAME)

        if not os.path.exists(canonical_results_path):
            yatest_logger.debug(
                "Test %s does not have canonical results by %s", test_description, canonical_results_path
            )
            return None

        results = self._get_canonical_file_for_test(canonical_results_path, test_description)
        if test_name in results:
            return results[test_name]

        # Try to find test results by legacy name
        legacy_test_name = self._get_legacy_test_name(project_path, test_name)
        if legacy_test_name and legacy_test_name in results:
            yatest_logger.debug("Test %s result is found by legacy name: '%s'", test_description, legacy_test_name)
            return results[legacy_test_name]

        return None

    def _try_to_get_canonical_results_path(self, project_path, test_name, test_description):
        canonical_results_path = os.path.join(
            self.get_test_canon_dir(project_path, test_name), const.CANON_RESULT_FILE_NAME
        )
        if os.path.exists(canonical_results_path):
            return canonical_results_path
        else:
            yatest_logger.debug(
                "Test %s does not have canonical results by %s", test_description, canonical_results_path
            )
            return None

    def _get_legacy_test_name(self, project_path, test_name):
        # Remove project name from test module name
        prefix = project_path.replace('/', '.').replace('-', '_') + '.'
        if test_name.startswith(prefix):
            return test_name[len(prefix) :]
        else:
            return None

    @exts.func.memoize()
    def _get_canonical_file(self, filename):
        with open(filename) as results_file:
            return common.strings_to_utf8(json.load(results_file))

    def _get_canonical_file_for_test(self, filename, test_description):
        try:
            res = self._get_canonical_file(filename)
        except Exception as e:
            raise Exception(
                "Looks like canonical file '{}' for test '{}' is broken: {}".format(filename, test_description, e)
            )
        return res

    def canonize(self, suite):
        res = True
        upload_targets = []
        temp_test_results_folders = {}
        with tmp.temp_dir() as results_root, tmp.temp_dir() as root_dir_for_extracted_files:
            deselected = set()
            for chunk in suite.chunks:
                if chunk.get_status() != test_const.Status.GOOD and chunk.has_comment():
                    yatest_logger.error("%s has error: %s", chunk.get_name(), chunk.get_comment())
                    res = False
            for test_case in suite.tests:
                if test_case.status in (
                    test_const.Status.DESELECTED,
                    test_const.Status.SKIPPED,
                    test_const.Status.XFAIL,
                    test_const.Status.XPASS,
                ):
                    continue
                elif test_case.status == test_const.Status.XFAILDIFF:
                    # XFAILDIFF is internal status and shouldn't
                    test_case.status = test_const.Status.XFAIL
                    continue
                elif test_case.status != test_const.Status.GOOD:
                    yatest_logger.error("Cannot canonize broken test %s: %s", test_case, test_case.comment)
                    res = False
                    continue

                try:
                    canonical_name = self._get_canonical_test_name(suite, test_case)
                    suitable_canonical_name = self._get_canonical_filename(canonical_name)
                    canonical_result_dir = os.path.join(results_root, suitable_canonical_name)
                    if test_case.result is not None:
                        canonical_results = self.get_canonical_results(suite.project_path, canonical_name)
                        dir_for_extracted_files = os.path.join(root_dir_for_extracted_files, suitable_canonical_name)
                        exts.fs.ensure_dir(dir_for_extracted_files)
                        test_case.result = self._prepare_result(
                            test_case.result,
                            canonical_result_dir,
                            dir_for_extracted_files,
                            canonical_results,
                            upload_targets,
                        )
                        if external_diff_tool_check(test_case.result, canonical_results):
                            comparer = test_canon_compare.ResultsComparer(
                                canonical_name,
                                self._sandbox_storage,
                                self.get_test_canon_dir(suite.project_path, ""),
                                self.get_test_canon_dir(suite.project_path, canonical_name),
                                suite.output_dir(),
                                max_file_diff_length=self._max_inline_diff_size,
                                mds_storage=self._mds_storage,
                            )
                            should_be_canonized, d = comparer.diff(test_case.result, canonical_results)
                            if not should_be_canonized:
                                deselected.add(canonical_name)
                        temp_test_results_folders[canonical_name] = canonical_result_dir
                except Exception as e:
                    yatest_logger.error(traceback.format_exc())
                    yatest_logger.error("Error while canonizing %s from %s: %s", test_case.name, suite.project_path, e)
                    test_case.comment = str(e)
                    test_case.status = test_const.Status.FAIL
                    res = False

            for upload_targets_group in self._group_upload_targets(upload_targets):
                if self._no_src_changes:
                    yatest_logger.debug("Skipping uploading")
                elif app_config.in_house:
                    if self._mds:
                        upload.upload_to_mds(
                            upload_targets_group,
                            results_root,
                            self._uploaded_resource_ttl,
                            self._mds_token,
                            self._oauth_token,
                            self._backend,
                        )
                    else:
                        upload.upload_to_sandbox(
                            upload_targets_group,
                            results_root,
                            suite,
                            self._resource_owner,
                            self._uploaded_resource_ttl,
                            self._sandbox_url,
                            self._sandbox_token,
                            self._upload_transport,
                            self._ssh_keys,
                            self._ssh_user,
                            self._skynet_upload_task_state_printer,
                        )
                else:
                    yatest_logger.debug("Skipping upload")

            def fix_canonical_result(canonical_result):
                # the field is needed to check the use of diff_tool
                # we don't need to canonize this field
                if isinstance(canonical_result, list):
                    for res in canonical_result:
                        fix_canonical_result(res)
                if isinstance(canonical_result, dict):
                    if "diff_tool" in canonical_result:
                        del canonical_result["diff_tool"]
                    else:
                        for k, v in canonical_result.items():
                            fix_canonical_result(v)

            suite_result = {}
            for test_case in suite.tests:
                canonical_name = self._get_canonical_test_name(suite, test_case)
                if test_case.status == test_const.Status.GOOD and canonical_name not in deselected:
                    if canonical_name in temp_test_results_folders:
                        test_case_result = copy.deepcopy(test_case.result)
                        if test_case_result is not None:
                            fix_canonical_result(test_case_result)
                            suite_result[canonical_name] = test_case_result
                else:
                    current_res = self._copy_test_canonical_results(results_root, suite.project_path, canonical_name)
                    if current_res is not None:
                        fix_canonical_result(current_res)
                        yatest_logger.debug(
                            "Will copy the current result of test %s with status %s", canonical_name, test_case.status
                        )
                        suite_result[canonical_name] = current_res
                    else:
                        yatest_logger.debug("Test %s does not have canonical result", canonical_name)

            def set_proper_external_path(value, _):
                # this is needed to transform full paths that come from the save method in the run_test node
                # to relative ones to be saved in the results.json files
                # also, drop precomputed checksums from local files: they are redudant in a repository
                if external.is_external(value):
                    external_data = external.ExternalDataInfo(value)
                    if external_data.is_file:
                        # NOTE: checksum is omitted here
                        return dict(
                            external.ExternalDataInfo.serialize_file(os.path.relpath(external_data.path, results_root))
                        )
                    return value
                return value

            for d in os.listdir(results_root):
                d = os.path.join(results_root, d)
                if not os.listdir(d):
                    exts.fs.remove_dir(d)

            suite_result = external.apply(set_proper_external_path, suite_result)
            if suite.save_old_canondata:
                canonical_results_path = os.path.join(
                    self.get_suite_canon_dir(suite.project_path), const.CANON_RESULT_FILE_NAME
                )
                suite_result_saved = self._get_canonical_file(canonical_results_path)
                canon_dir = self.get_suite_canon_dir(suite.project_path)
                data_list = os.listdir(canon_dir)
                for k, v in suite_result_saved.items():
                    if k not in suite_result:
                        suite_result[k] = v
                        if k in data_list:
                            shutil.copytree(os.path.join(canon_dir, k), os.path.join(results_root, k))
            if suite_result:
                if self._backend:
                    recursive_replace_uri_pattern(suite_result, self._backend, _replace_backend_to_pattern)

                if self._no_src_changes:
                    yatest_logger.warning("Skip applying changes to canodata - no src changes were requested")
                    return res

                with open(os.path.join(results_root, const.CANON_RESULT_FILE_NAME), "w", newline='\n') as result_file:
                    json.dump(
                        suite_result, result_file, indent=4, ensure_ascii=False, separators=(',', ': '), sort_keys=True
                    )
                    result_file.write('\n')

                self._apply_changes(self.get_suite_canon_dir(suite.project_path), results_root)
        return res

    def save(self, suite):
        for test_case in suite.tests:
            try:
                test_case.result = self._save_test(
                    self._get_canonical_test_name(suite, test_case), test_case.result, suite.output_dir()
                )
            except Exception as e:
                yatest_logger.debug(traceback.format_exc())
                message = "Error while saving results {} from {}: {}".format(test_case.name, suite.project_path, e)
                yatest_logger.error(message)
                test_case.comment = message
                test_case.status = test_const.Status.FAIL

    def verify(self, suite, verification_failed_status=test_const.Status.FAIL):
        output_dir = suite.output_dir()
        canondir = self.get_suite_canon_dir(suite.project_path)
        canondir_exists = os.path.exists(canondir)

        for test_case in suite.tests:
            # Speed up verification for tests without canonization
            if not canondir_exists and test_case.result is None and not test_case.is_diff_test:
                # Set xfail status - test explicitly specified that it should return canon data
                if test_case.status == test_const.Status.XFAILDIFF:
                    test_case.status = test_const.Status.XFAIL

                yatest_logger.debug("Test %s doesn't use canonization", test_case.name)
                continue

            if not test_case.logs.get("log"):
                test_case.logs["log"] = common.get_test_log_file_path(output_dir, test_case.name)
            with exts.log.add_handler(test_tools.get_common_logging_file_handler(test_case.logs["log"])):
                if test_case.status in (test_const.Status.GOOD, test_const.Status.XFAILDIFF):
                    try:
                        if test_case.is_diff_test:
                            self._process_diff_test(suite, test_case)
                        else:
                            self._verify_test(
                                suite.project_path,
                                self._get_canonical_test_name(suite, test_case),
                                test_case.result,
                                output_dir,
                            )
                    except DiffTestResultSanityCheckException as e:
                        test_case.comment = "[[bad]]{}[[rst]]".format(e)
                        test_case.status = test_const.Status.FAIL
                    except (CanonicalDataMissingError, CanonicalResultVerificationException, AssertionError) as e:
                        error = str(e)

                        if test_case.status == test_const.Status.XFAILDIFF:
                            if isinstance(e, CanonicalDataMissingError):
                                test_case.comment = "[[warn]]Test case is makred with [[imp]]xfaildiff[[warn]]: no canon data found as expected"
                            else:
                                test_case.comment = "[[warn]]Test results expectedly differ ([[imp]]xfaildiff[[warn]]) from canonical:[[unimp]]\n{}".format(
                                    error
                                )
                            test_case.status = test_const.Status.XFAIL
                        else:
                            test_case.comment = "[[bad]]Test results differ from canonical:\n{}".format(error)
                            test_case.status = verification_failed_status

                        if getattr(e, 'diff_path', None):
                            test_case.logs["diff"] = e.diff_path
                    except Exception:
                        test_case.comment = "[[bad]]Unexpected error while verifying canonical results:\n{}".format(
                            traceback.format_exc()
                        )
                        test_case.status = test_const.Status.FAIL
                    else:
                        # verification succeed, check that it's expected
                        if test_case.status == test_const.Status.XFAILDIFF:
                            test_case.comment = "[[bad]]Test results has no diff with canonical data. However, test case is marked as [[imp]]xfaildiff[[bad]] and expected to have a diff"
                            test_case.status = test_const.Status.XPASS

                else:
                    yatest_logger.debug("Test did not pass, will not verify test output with possible canonical one")
                yatest_logger.debug(
                    "Test %s status: %s",
                    self._get_canonical_test_name(suite, test_case),
                    test_const.Status.TO_STR[test_case.status],
                )
                if test_case.comment:
                    yatest_logger.debug(os.linesep + common.to_utf8(strip_markup(test_case.comment)))

    def _process_diff_test(self, suite, test_case):
        def process_diff(value, value_path):
            if any(
                [
                    not value_path and (type(value) is not dict or external.is_external(value)),
                    value_path and not external.is_external(value),
                    len(value_path) > 1,
                    type(test_case.result) is not dict,
                ]
            ):
                raise DiffTestResultSanityCheckException(
                    "diff test result must be dict of <key>->yatest.common.canonical_file()"
                )

            err = "Error in diff test result['{}']".format(value_path)
            if value_path:
                value_path = value_path[0]
                if not external.is_external(value):
                    raise DiffTestResultSanityCheckException(
                        "{}: {}".format(err, "value must be a yatest.common.canonical_file")
                    )

                external_data = external.ExternalDataInfo(value)
                if not external_data.is_file:
                    raise DiffTestResultSanityCheckException("{}: {}".format(err, "only local files are supported"))

                if value_path in ["log", "logsdir"]:
                    raise DiffTestResultSanityCheckException("Cannot used reserved key name '{}'".format(value_path))
                test_case.logs[value_path] = external_data.path

        if not test_case.result:
            raise DiffTestResultSanityCheckException("diff test must have a result")

        test_case.result = self._save_test(
            self._get_canonical_test_name(suite, test_case), test_case.result, suite.output_dir(), is_diff_test=True
        )
        external.apply(process_diff, test_case.result)

    def _verify_test(self, project_path, test_name, test_result, output_path):
        yatest_logger.debug("Verifying %s canonical results", self._get_test_full_name(project_path, test_name))
        canonical_results = self.get_canonical_results(project_path, test_name)
        if canonical_results is not None:
            comparer = test_canon_compare.ResultsComparer(
                test_name,
                self._sandbox_storage,
                self.get_test_canon_dir(project_path, ""),
                self.get_test_canon_dir(project_path, test_name),
                output_path,
                max_file_diff_length=self._max_inline_diff_size,
                mds_storage=self._mds_storage,
            )
            diff, diff_path = comparer.diff(test_result, canonical_results)
            if diff:
                raise CanonicalResultVerificationException(common.to_utf8(diff), diff_path)
        else:
            if test_result is not None:
                raise CanonicalDataMissingError(
                    "No canonical data for test {}, canonize test results using -Z option".format(test_name)
                )

    def _save_test(self, test_name, test_result, output_path, is_diff_test=False):
        result_dir = os.path.join(output_path, "results", self._get_canonical_filename(test_name))
        exts.fs.create_dirs(result_dir)
        return self._prepare_result(test_result, result_dir, result_dir, force_save_locally=is_diff_test)

    def _group_upload_targets(self, upload_targets, target_group_size=DEFAULT_FILE_GROUP_SIZE):
        if not target_group_size:
            return [upload_targets]
        targets = [(external.ExternalDataInfo(info).size or 0, info) for info in upload_targets]
        #  simple bin packing algorithm that groups small files together
        result_groups = [[]]
        last_group_size = 0
        for size, info in sorted(targets, key=lambda t: t[0], reverse=True):
            if last_group_size and last_group_size + size > target_group_size * 5 // 4:
                result_groups.append([])
                last_group_size = 0
            result_groups[-1].append(info)
            last_group_size += size
            if last_group_size >= target_group_size:
                result_groups.append([])
                last_group_size = 0
        if not result_groups[-1]:
            result_groups.pop()
        return result_groups

    def _prepare_result(
        self,
        test_result,
        result_folder,
        dir_for_extracted_files,
        current_result=None,
        upload_targets=None,
        force_save_locally=False,
    ):
        def prepare(value, value_path):
            if external.is_external(value):
                external_data = external.ExternalDataInfo(value)
                if external_data.is_file:
                    return self._save_path(
                        result_folder,
                        external_data.path,
                        value_path,
                        current_result,
                        force_save_locally or external_data.get("local", False),
                        upload_targets,
                        diff_tool=external_data.get("diff_tool"),
                        diff_tool_timeout=external_data.get("diff_tool_timeout"),
                    )
                return value
            if isinstance(value, six.string_types):
                if len(value) > self._max_str_len:
                    return self._extract(
                        dir_for_extracted_files, result_folder, value, value_path, current_result, upload_targets
                    )
            return value

        if test_result is not None:
            modified_test_result = copy.deepcopy(test_result)
            modified_test_result = external.apply(prepare, modified_test_result)
            return modified_test_result

    def _extract(self, extract_dir, canonical_dir, value, value_path, current_result, delayed_upload_args):
        extracted_path = common.get_unique_file_path(extract_dir, "extracted")
        with open(extracted_path, "w", newline='\n') as extracted:
            extracted.write(value)
        save_locally = self._should_be_saved_locally(value, extracted_path)
        return self._save_path(
            canonical_dir, extracted_path, value_path, current_result, save_locally, delayed_upload_args
        )

    def _should_be_saved_locally(self, value, filename):
        locally = True
        if self._max_file_size:
            locally &= len(value) < self._max_file_size
        locally &= self._valid_for_vcs(value, filename)
        return locally

    def _valid_for_vcs(self, value, filename):
        # CRLF is prohibited in the repository. See ARC-1172
        crlf_found = "\r\n" not in value
        if crlf_found:
            yatest_logger.debug("Found CRLF in the %s - going to upload instead of storing locally", filename)
        return crlf_found

    def _save_path(
        self,
        canonical_dir,
        saving_path,
        result_path,
        current_result,
        save_locally,
        delayed_upload_args,
        diff_tool=None,
        diff_tool_timeout=None,
    ):
        exts.fs.ensure_dir(canonical_dir)
        checksum = exts.hashing.md5_path(saving_path, include_dir_layout=True)

        # check if the current result is the same - no need to reupload it
        if current_result:
            current = current_result
            for path in result_path:
                try:
                    current = current[path]
                except (IndexError, KeyError, TypeError):
                    current = None
                    break
            if current and external.is_external(current):
                current = external.ExternalDataInfo(current)
                if current.checksum == checksum:
                    if current.is_sandbox_resource or current.is_http:
                        yatest_logger.debug("Using the existing data %s", current)
                        return dict(current.serialize())
                    else:
                        # we are dealing with the existing file saved locally, probably by older canonization,
                        # so leave it as is
                        if not save_locally:
                            save_locally = True

        # save file in repository
        canonical_file_pattern = self._get_canonical_filename(os.path.basename(saving_path))
        canonical_path = common.get_unique_file_path(
            canonical_dir, canonical_file_pattern, create_file=not os.path.isdir(saving_path)
        )
        if os.path.isdir(saving_path):
            exts.fs.copytree3(saving_path, canonical_path, dirs_exist_ok=True)
        else:
            exts.fs.copy_file(saving_path, canonical_path)

        if save_locally or not self._max_file_size:
            saved = dict(
                external.ExternalDataInfo.serialize_file(
                    canonical_path,
                    checksum,
                    diff_tool=diff_tool,
                    diff_tool_timeout=diff_tool_timeout,
                    local=save_locally,
                    size=os.path.getsize(canonical_path),
                )
            )
        else:
            saved = dict(
                external.ExternalDataInfo.serialize_file(
                    canonical_path,
                    checksum,
                    diff_tool=diff_tool,
                    diff_tool_timeout=diff_tool_timeout,
                    size=os.path.getsize(canonical_path),
                )
            )
            delayed_upload_args.append(saved)

        return saved

    def _apply_changes(self, test_canonical_dir, temp_canonical_dir):
        if os.path.exists(test_canonical_dir):
            yatest_logger.debug("Deleting old canonical dir %s", test_canonical_dir)
            shutil.rmtree(test_canonical_dir)

        if temp_canonical_dir and os.path.exists(temp_canonical_dir):
            exts.fs.copy_tree(temp_canonical_dir, test_canonical_dir)

        return self.repo().apply(test_canonical_dir)

    def _get_test_full_name(self, project_path, test_name):
        return "[{} in {}]".format(test_name, project_path)

    def get_test_canon_dir(self, project_path, test_name):
        return os.path.join(
            self._arc_path,
            project_path,
            const.CANON_DATA_DIR_NAME,
            self._sub_path or "",
            self._get_canonical_filename(test_name),
        )

    def get_suite_canon_dir(self, project_path):
        return os.path.join(self._arc_path, project_path, const.CANON_DATA_DIR_NAME, self._sub_path or "")

    def _get_canonical_filename(self, name, lenght_limit=200):
        # don't generate filenames with non-ascii characters that will be committed into the repository
        unicode_free_name = ""

        for symbol in six.ensure_text(name, 'UTF-8'):
            code = ord(symbol)
            if 31 < code < 128:
                unicode_free_name += chr(code)
            else:
                unicode_free_name += str(hex(code))
        # Remove reserved characters
        name = re.sub(r"[\[,\]'<>\:\"\/\\|?*]", "_", unicode_free_name)
        # Reduce filename length to avoid hitting FS limit
        if len(name) > lenght_limit:
            hashsum = exts.hashing.fast_hash(name)
            name = "{}-{}".format(name[: lenght_limit - len(hashsum) - 1], hashsum)
        return name

    def _get_canonical_test_name(self, suite, test_case):
        test_name_split = test_case.name.split(test_const.TEST_SUBTEST_SEPARATOR)
        test_name = test_name_split[0]
        test_class_name = ".".join(test_name_split[1:])

        if test_name.endswith(".py"):
            test_name = test_name[: -len(".py")]
            return "{}.{}".format(test_name, test_class_name)
        elif test_name == "exectest":  # XXX
            return "{}.{}".format(test_name, test_class_name)
        else:
            # XXX: remove suite.name
            return "{}.{}.{}".format(suite.name, test_name, test_class_name)

    def _get_repo(self, dummy=False):
        if dummy:
            yatest_logger.debug("Dummy repo requested")
            return DummyRepo(self._arc_path)

        vcs_type, _, _ = detect([self._arc_path])
        yatest_logger.debug("Detected vcs: %s", vcs_type)
        if 'svn' in vcs_type:
            repo = SvnRepo(self._arc_path)
        elif 'hg' in vcs_type:
            repo = HgRepo(self._arc_path)
        elif 'arc' in vcs_type:
            repo = ArcRepo(self._arc_path)
        else:
            repo = DummyRepo(self._arc_path)

        yatest_logger.debug("%s is %s", type(repo), 'active' if repo.active() else 'not active')
        if not repo.active():
            yatest_logger.debug("Falling back to DummyRepo")
            repo = DummyRepo(self._arc_path)

        return repo
