import os
import copy
import exts.yjson as json
import logging
import threading

from collections import defaultdict
from itertools import chain

import devtools.ya.core.error
import devtools.ya.test.common as test_common
import yalibrary.term.console as term_console
from devtools.ya.build.reports import configure_error as ce
from exts.fs import create_dirs, ensure_removed
import devtools.ya.build.owners as ow
from devtools.ya.test import const as constants
from devtools.ya.test.const import Status, TestSize
from devtools.ya.test.reports import trace_comment
import devtools.ya.test.reports.report_prototype as rp
import devtools.ya.test.util.tools as tut
from yalibrary import formatter
from yalibrary import platform_matcher

from . import results_report

logger = logging.getLogger(__name__)


TEST_SIZES = [TestSize.Small, TestSize.Medium, TestSize.Large]
TIMEOUT_TARGETS_MARKER = "Process exceeds time limit"
DEPENDS_ON_BROKEN_TARGETS_MARKER = "Depends on broken targets"
IMPORTANT_FIELDS = {'path', 'toolchain', 'duration'}


def truncate_snippet(record):
    if record.get("rich-snippet"):
        record["rich-snippet"] = trace_comment.truncate_comment(record["rich-snippet"], constants.REPORT_SNIPPET_LIMIT)
    return record


def remove_empty_field(entry):
    for k, v in tuple(entry.items()):
        if not v and k not in IMPORTANT_FIELDS:
            del entry[k]
    return entry


def filter_nontest_node(entry):
    if entry.get("type") in ["style", "test"]:
        return True
    return False


def _fix_link_prefix_and_quote(link, fix_from, fix_to):
    assert link, locals()
    if link.startswith(fix_from):
        tail = link[len(fix_from) :]
        url = "/".join([_f for _f in [fix_to.rstrip("/"), formatter.html.quote_url(tail)] if _f])
        if url.endswith((".tar", ".tar.gz", ".tar.zstd")):
            return url + "/"
        return url
    else:
        return formatter.html.quote_url(link)


def _fix_links_entry(entry, name, fix_from, fix_to):
    paths = entry["links"][name]
    assert isinstance(paths, list), entry
    for i in range(len(paths)):
        if fix_to.startswith("http") and not paths[i].startswith("http"):
            p = paths[i]
            if os.sep == "\\":  # windows host
                p = p.replace("\\", "/")
            paths[i] = _fix_link_prefix_and_quote(p, fix_from, fix_to)
        else:
            paths[i] = paths[i].replace(fix_from, fix_to)
    entry["links"][name] = paths

    if "rich-snippet" in entry:
        entry["rich-snippet"] = entry["rich-snippet"].replace(fix_from, fix_to)


def _fix_links(entry, fix_from, fix_to):
    for name in entry.get("links", {}).keys():
        _fix_links_entry(entry, name, fix_from, fix_to)
    for obj in entry.get("result", {}).values():
        if fix_to.startswith("http"):
            obj['url'] = _fix_link_prefix_and_quote(obj['url'], fix_from, fix_to)
        else:
            obj['url'] = obj['url'].replace(fix_from, fix_to)
    if "rich-snippet" in entry:
        entry["rich-snippet"] = entry["rich-snippet"].replace(fix_from, fix_to)
    return entry


def log_restrained_investigation(entry, logsdir, output_dir, work_dir, tar_dirs):
    ncalls = getattr(log_restrained_investigation, 'ncalls', 0)
    setattr(log_restrained_investigation, 'ncalls', ncalls + 1)

    if ncalls > 20:
        logger.debug('Unable to find link file for %s', logsdir)
    else:

        def format_path(path):
            return "{}(exist:{})".format(path, os.path.exists(path))

        logger.debug(
            "Unable to find link file for %s. Test entry: %s", logsdir, json.dumps(entry, indent=4, sort_keys=True)
        )
        logger.debug(
            "Extracted logsdir:%s output_dir:%s: work_dir:%s",
            format_path(logsdir),
            format_path(output_dir),
            format_path(work_dir),
        )
        for path in tar_dirs:
            logger.debug("Target dir %s content: %s", path, os.listdir(path) if os.path.exists(path) else 'None')


def _replace_logs_with_links(entry, output_dir, work_dir, links_map=None):
    def _any_path():
        for log_type, paths in entry.get("links", {}).items():
            for path in paths:
                any_path = path.replace("build-release", "")
                return any_path
        return None

    def _build_root():
        path = _any_path()
        if not path:
            return output_dir

        while not work_dir.endswith(path) and path != '/':
            path, _ = os.path.split(path)

        if path in ['', '/']:
            return output_dir

        return work_dir[: work_dir.find(path)]

    search_dir = output_dir
    if os.path.exists(work_dir) and os.path.isdir(work_dir):
        search_dir = _build_root()

    if links_map is None:
        links_map = {}

    path_to_link = dict()
    links = set()

    for log_type, paths in entry.get("links", {}).items():
        read_files = True

        if links_map:
            for path in paths:
                link_path = path + '.link'
                link_tar_path = path + '.tar.link'
                link_tar_zstd_path = path + '.tar.zstd.link'

                if link := links_map.get(link_path):
                    links.add(link)
                    _fix_links_entry(entry, log_type, path, link)
                    read_files = False
                    break

                link_key = None

                if link_tar_path in links_map:
                    link_key = link_tar_path
                elif link_tar_zstd_path in links_map:
                    link_key = link_tar_zstd_path
                if link_key:
                    path_to_link[path] = links_map[link_key]
                    read_files = False
                    break

        if read_files:
            # XXX
            # Fallback to reading from FS
            for path in paths:
                link_path = path.replace("build-release", search_dir) + '.link'
                link_tar_path = path.replace("build-release", search_dir) + '.tar.link'
                link_tar_zstd_path = path.replace("build-release", search_dir) + '.tar.zstd.link'

                if os.path.exists(link_path) and os.path.isfile(link_path):
                    with open(link_path) as fp:
                        link = fp.read().strip()
                        links.add(link)
                        _fix_links_entry(entry, log_type, path, link)
                    break
                else:
                    link_filename = None
                    if os.path.exists(link_tar_path) and os.path.isfile(link_tar_path):
                        link_filename = link_tar_path
                    elif os.path.exists(link_tar_zstd_path) and os.path.isfile(link_tar_zstd_path):
                        link_filename = link_tar_zstd_path
                    if link_filename:
                        with open(link_filename) as fp:
                            link = fp.read().strip()
                            path_to_link[path] = link
                        break

    for log_type, paths in entry.get("links", {}).items():
        for path in paths:
            if path not in links:
                for dirpath in path_to_link:
                    if path.startswith(dirpath):
                        _fix_links_entry(entry, log_type, dirpath, path_to_link[dirpath])
                        break

    return entry


def fix_links(report, result_root, build_root='build-release'):
    entries = []
    for entry in report["results"]:
        entries.append(_fix_links(entry, build_root, result_root))
    report["results"] = entries
    return report


def parse_error_message(error, target_name):
    if not isinstance(error, ce.ConfigureError):
        error = ce.ConfigureError(str(error), 0, 0)

    detailed_message = {
        "path": target_name,
        "type": "ERROR",
        "text": error.message,
    }

    if error.row != 0 and error.column != 0:
        detailed_message['line'] = error.row
        detailed_message['position'] = error.column

    return detailed_message


def make_target_entry(
    target_name,
    target_id,
    target_hid,
    uid,
    status,
    owners,
    results_dir,
    messages_dir,
    errors,
    message_type,
    target_platform=None,
    metrics=None,
    report_config=None,
    module_tag=None,
    errors_links=None,
    error_type=None,
):
    def get_error_message(error):
        if isinstance(error, str):
            return error
        elif isinstance(error, ce.ConfigureError):
            return error.message
        else:
            logger.debug('Suspicious configure error type: %s (error: %s)', str(type(error)), str(error))
            return str(error)

    errors = sorted(errors or [])

    toolchain = results_report.transform_toolchain(
        report_config or {}, target_platform or platform_matcher.my_platform()
    )
    if results_report.is_toolchain_ignored(report_config, toolchain):
        return None

    entry = rp.get_blank_record()
    entry.update(
        {
            "id": target_id,
            # WIP: https://st.yandex-team.ru/DEVTOOLS-8716
            "hid": target_hid,
            "toolchain": toolchain,
            "path": target_name,
            "type": message_type,
            "status": status,
            "uid": uid,
            "owners": owners,
        }
    )
    if error_type:
        entry["error_type"] = error_type
    else:
        if status == rp.TestStatus.Fail:
            entry["error_type"] = rp.ErrorType.Regular
    if metrics:
        entry['metrics'].update(metrics)
    if module_tag:
        entry['name'] = module_tag

    if errors:
        messages_file = os.path.join(messages_dir, target_name, "{}-{}.txt".format(message_type, toolchain))
        create_dirs(os.path.dirname(messages_file))

        errors_text = test_common.to_utf8("\n\n".join(get_error_message(error) for error in errors))
        plain_errors = term_console.ecma_48_sgr_regex().sub("", errors_text)

        with open(messages_file, "w") as file:
            file.write(plain_errors)

        entry["rich-snippet"] = formatter.ansi_codes_to_markup(errors_text)
        # entry["links"]["messages"] = [os.path.relpath(messages_file, results_dir)]

        errors_links_flattened = list(chain(*[_f for _f in errors_links or [] if _f]))
        for i, link in enumerate(errors_links_flattened):
            suffix = '-{}'.format(i + 1)
            entry["links"]["stderr%s" % suffix] = [link]

        for error_item in errors:
            if get_error_message(error_item).startswith(DEPENDS_ON_BROKEN_TARGETS_MARKER):
                entry["error_type"] = rp.ErrorType.BrokenDeps
                break
            if get_error_message(error_item).startswith(TIMEOUT_TARGETS_MARKER):
                entry["error_type"] = rp.ErrorType.Timeout
                break

        if message_type == 'configure' and errors:
            entry['messages'] = [parse_error_message(error, target_name) for error in errors]

    return entry


class YaMakeProgress:
    BUILD = 'build'
    CONFIGURE = 'configure'
    STYLE = 'style'
    TEST = 'test'

    def __init__(self, totals_build, totals_style, totals_test_by_size):
        self._lock = threading.RLock()
        self.progress_without_test = defaultdict(lambda: defaultdict(lambda: {'done': 0, 'total': 0}))
        self.progress_only_test = defaultdict(lambda: defaultdict(lambda: {'done': 0, 'total': 0}))
        for toolchain, total in totals_build.items():
            self._increment(toolchain, YaMakeProgress.BUILD, total, progress_key='total')
        for toolchain, total in totals_style.items():
            self._increment(toolchain, YaMakeProgress.STYLE, total, progress_key='total')
        for toolchain, totals_test in totals_test_by_size.items():
            for size, total in totals_test.items():
                self._increment(toolchain, YaMakeProgress.TEST, total, progress_key='total', test_size=size)
        for toolchain in set(self.progress_without_test.keys()) | set(self.progress_only_test.keys()):
            self._increment(toolchain, YaMakeProgress.CONFIGURE, 0, progress_key='total')

    def _increment(self, toolchain, build_type, value, test_size=None, progress_key='done'):
        with self._lock:
            if test_size:
                self.progress_only_test[toolchain][test_size][progress_key] += value
            else:
                self.progress_without_test[toolchain][build_type][progress_key] += value

    def increment_configure_done(self, toolchain, value):
        """all graph in autocheck2.0 is read at once when starting report thus here total is equal to done"""
        self._increment(toolchain, YaMakeProgress.CONFIGURE, value)
        self._increment(toolchain, YaMakeProgress.CONFIGURE, value, progress_key='total')

    def increment_build_done(self, toolchain, value):
        self._increment(toolchain, YaMakeProgress.BUILD, value)

    def increment_style_done(self, toolchain, value):
        self._increment(toolchain, YaMakeProgress.STYLE, value)

    def increment_test_done(self, toolchain, test_size, value):
        self._increment(toolchain, YaMakeProgress.TEST, value, test_size=test_size)

    def get_progress_ci_format(self):
        with self._lock:
            progress_without_test = copy.deepcopy(self.progress_without_test)
            progress_only_test = copy.deepcopy(self.progress_only_test)

        ci_progress = defaultdict(list)
        for toolchain, progress_by_type in progress_without_test.items():
            for _type, type_progress in progress_by_type.items():
                ci_type_progress = type_progress.copy()
                ci_type_progress.update({'type': _type})
                ci_progress[toolchain] += [ci_type_progress]

        for toolchain, progress_by_size in progress_only_test.items():
            for size, size_progress in progress_by_size.items():
                ci_test_progress = size_progress.copy()
                ci_test_progress.update({'type': YaMakeProgress.TEST, 'size': size})
                ci_progress[toolchain] += [ci_test_progress]
        return ci_progress


class ReportGenerator:
    _logger = logging.getLogger('ReportGenerator')

    def __init__(
        self, opts, distbuild_graph, targets, tests, owners_list, make_files, results_dir, build_root, reports
    ):
        self._opts = opts
        self._distbuild_graph = distbuild_graph
        self._report_config = results_report.safe_read_report_config(self._opts.report_config_path)
        self._targets = targets
        self._owners_list = owners_list
        self._make_files = make_files
        self._results_dir = results_dir
        self._build_root = build_root
        self._reports = reports
        self._messages_dir = os.path.join(results_dir, "compiler-messages")
        ensure_removed(self._messages_dir)
        create_dirs(self._messages_dir)
        self._processed_lock = threading.Lock()
        self._processed_uids = set()
        self._tests_by_size = defaultdict(set)
        tests_count = defaultdict(lambda: defaultdict(int))
        style_tests_count = defaultdict(int)
        logger.debug("Got %d tests to report", len(tests))
        for test in tests:
            toolchain = test.target_platform_descriptor
            transformed_toolchain = results_report.transform_toolchain(self._report_config, toolchain)
            should_report = (
                test.is_skipped() and (self._opts.report_skipped_suites_only or self._opts.report_skipped_suites)
            ) or (not test.is_skipped() and not self._opts.report_skipped_suites_only)
            if test.get_ci_type_name() == YaMakeProgress.TEST:
                self._tests_by_size[test.test_size].add(test.uid)
                tests_count[transformed_toolchain][test.test_size] += 1 * should_report
            elif test.get_ci_type_name() == YaMakeProgress.STYLE:
                style_tests_count[transformed_toolchain] += 1 * should_report

        self._tests_by_type = defaultdict(set)
        for test in tests:
            self._tests_by_type[test.get_ci_type_name()].add(test.uid)
        for size in TEST_SIZES:
            if size not in self._tests_by_size:
                self.finish_tests_report_by_size(size)
                self._tests_by_size[size] = set()
        self.setup_ya_make_progress(style_tests_count, tests_count)
        self._links_map = {}

    def setup_ya_make_progress(self, style_tests_count, tests_count):
        totals_build = defaultdict(int)
        for target in self._targets.values():
            _, target_platform, _, _, _ = target
            transformed_toolchain = results_report.transform_toolchain(self._report_config, target_platform)
            totals_build[transformed_toolchain] += 1 * (not self._opts.report_skipped_suites_only)

        self.ya_make_progress = YaMakeProgress(totals_build, style_tests_count, tests_count)
        for x in self._reports:
            x.set_progress_channel(self.ya_make_progress.get_progress_ci_format)

    def finish(self):
        self._logger.debug('Finish report')
        for report in self._reports:
            report.finish()

    def finish_style_report(self):
        self._logger.debug('Finish style report')
        for report in self._reports:
            report.finish_style_report()

    def finish_configure_report(self):
        self._logger.debug('Finish configure report')
        for report in self._reports:
            report.finish_configure_report()

    def finish_build_report(self):
        self._logger.debug('Finish build report')
        for report in self._reports:
            report.finish_build_report()

    def finish_tests_report(self):
        self._logger.debug(
            'Finish tests report, tests by size %s, tests by type %s ', self._tests_by_size, self._tests_by_type
        )
        for size in self._tests_by_size.keys():
            self.finish_tests_report_by_size(size)
        for report in self._reports:
            report.finish_tests_report()

    def finish_tests_report_by_size(self, size):
        self._logger.debug('Finish tests report by size %s', size)
        for report in self._reports:
            report.finish_tests_report_by_size(size)

    def _post_process(self, entry):
        if not entry:
            return None
        if (
            self._opts.build_results_report_tests_only or self._report_config.get('report_tests_only')
        ) and not filter_nontest_node(entry):
            return None
        entry["rich-snippet"] = formatter.ansi_codes_to_markup(entry.get("rich-snippet", ""))
        entry = remove_empty_field(entry)
        entry = truncate_snippet(entry)
        return entry

    def _add_entries(self, entries):
        filtered_entries = [_f for _f in map(self._post_process, entries) if _f]
        for report in self._reports:
            report(filtered_entries)

    def add_configure_results(self, configure_errors):
        entries = []
        if getattr(self._opts, 'add_changed_ok_configures', False):
            progress_by_toolchain = defaultdict(int)
            for target_name in sorted(configure_errors.keys()):
                errors = [
                    configure_error
                    for configure_error in configure_errors.get(target_name, [])
                    if configure_error.message != 'OK'
                ]
                target_id = rp.get_id(target_name, 'configure-step')
                target_hid = rp.get_hash_id(target_name, 'configure-step')
                owners = ow.find_path_owners(self._owners_list, target_name)
                status = rp.TestStatus.Fail if errors else rp.TestStatus.Good
                configure_uid = rp.get_id(target_name, "configure" + str(errors))
                entries.append(
                    make_target_entry(
                        target_name,
                        target_id,
                        target_hid,
                        configure_uid,
                        status,
                        owners,
                        self._results_dir,
                        self._messages_dir,
                        errors,
                        'configure',
                        report_config=self._report_config,
                        module_tag=None,
                    )
                )
                toolchain = entries[-1].get('toolchain', None) if entries[-1] else None
                if toolchain:
                    progress_by_toolchain[toolchain] += 1
        else:
            targets = set()

            for target in self._targets.values():
                targets.add(target[0])

            for target_name in configure_errors.keys():
                targets.add(target_name)

            for target_name in self._make_files:
                targets.add(target_name)

            progress_by_toolchain = defaultdict(int)

            for target_name in sorted(targets):
                errors = configure_errors.get(target_name, [])
                target_id = rp.get_id(target_name, 'configure-step')
                target_hid = rp.get_hash_id(target_name, 'configure-step')
                owners = ow.find_path_owners(self._owners_list, target_name)
                status = rp.TestStatus.Fail if errors else rp.TestStatus.Good
                configure_uid = rp.get_id(target_name, "configure" + str(errors))
                entries.append(
                    make_target_entry(
                        target_name,
                        target_id,
                        target_hid,
                        configure_uid,
                        status,
                        owners,
                        self._results_dir,
                        self._messages_dir,
                        errors,
                        'configure',
                        report_config=self._report_config,
                        module_tag=None,
                    )
                )
                toolchain = entries[-1].get('toolchain', None) if entries[-1] else None
                if toolchain:
                    progress_by_toolchain[toolchain] += 1

        if self.ya_make_progress:
            list(
                map(
                    lambda toolchain_value: self.ya_make_progress.increment_configure_done(*toolchain_value),
                    progress_by_toolchain.items(),
                )
            )
        self._add_entries(entries)

    def _make_build_result(
        self, uid, target_name, target_platform, errors, metrics, module_tag, errors_links, exit_code=None
    ):
        with self._processed_lock:
            if not self._distbuild_graph.add_to_report(uid) or uid in self._processed_uids:
                return None
            self._processed_uids.add(uid)

        if errors:
            status = rp.TestStatus.Fail
            if exit_code == devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR:
                error_type = rp.ErrorType.Internal
            else:
                error_type = rp.ErrorType.Regular
        else:
            status = rp.TestStatus.Good
            error_type = None

        owners = ow.find_path_owners(self._owners_list, target_name)
        target_id = rp.get_id(target_name, subtest=(module_tag or ''))
        target_hid = rp.get_hash_id(target_name, subtest=(module_tag or ''))
        return make_target_entry(
            target_name,
            target_id,
            target_hid,
            uid,
            status,
            owners,
            self._results_dir,
            self._messages_dir,
            errors,
            'build',
            target_platform=target_platform,
            metrics=metrics,
            report_config=self._report_config,
            module_tag=module_tag,
            errors_links=errors_links,
            error_type=error_type,
        )

    def add_build_result(self, uid, target_name, target_platform, errors, metrics, module_tag, errors_links, exit_code):
        entry = self._make_build_result(
            uid, target_name, target_platform, errors, metrics, module_tag, errors_links, exit_code=exit_code
        )
        toolchain = entry.get('toolchain', None) if entry else None
        if self.ya_make_progress and toolchain:
            self.ya_make_progress.increment_build_done(toolchain, 1)
        self._add_entries([entry])

    def add_build_results(self, build_result):
        entries = []
        progress_by_toolchain = defaultdict(int)
        for uid, target in self._targets.items():
            target_name, target_platform, _, module_tag, _ = target
            key = (target_name, target_platform, uid)
            errors = build_result.build_errors.get(key)
            errors_links = build_result.build_errors_links.get(key, [])
            exit_code = build_result.exit_code_map.get(uid, 0)
            metrics = build_result.build_metrics.get(uid)
            entry = self._make_build_result(
                uid, target_name, target_platform, errors, metrics, module_tag, errors_links, exit_code=exit_code
            )
            if entry is not None:
                entries.append(entry)
                toolchain = entry.get('toolchain', None)
                if toolchain:
                    progress_by_toolchain[toolchain] += 1

        self._logger.debug('Build result is processed, add %s entries', len(entries))
        if self.ya_make_progress:
            list(
                map(
                    lambda toolchain_value: self.ya_make_progress.increment_build_done(*toolchain_value),
                    progress_by_toolchain.items(),
                )
            )
        self._add_entries(entries)

    def _finish_report_by_type(self, tp):
        if tp == 'test':
            self.finish_tests_report()
        elif tp == 'style':
            self.finish_style_report()
        else:
            logger.warning('Unable to close report by type %s', tp)

    def _fix_roots(self, entry, resolver):
        from yatest_lib import external

        def resolve_external_path(value, _):
            if external.is_external(value):
                value["uri"] = resolver.substitute(value["uri"])
            return value

        entry["rich-snippet"] = resolver.substitute(entry["rich-snippet"])
        entry["links"] = {k: [resolver.substitute(item) for item in v] for k, v in entry["links"].items()}
        if "result" in entry:
            external.apply(resolve_external_path, entry["result"])

    def _add_test_result(self, suite, node_errors, links, report_prototype):
        import devtools.ya.test.const as test_const

        with self._processed_lock:
            if suite.uid in self._processed_uids:
                return
            self._processed_uids.add(suite.uid)

        if suite.uid in report_prototype:
            entry_prototypes = report_prototype[suite.uid]
        else:
            entry_prototypes = rp.make_suites_results_prototype([suite])

        suite_prototypes = [entry for entry in entry_prototypes if "suite_hid" not in entry]
        assert len(suite_prototypes) == 1
        suite_prototype = suite_prototypes[0]

        test_size = suite_prototype["test_size"]
        ci_type_name = suite_prototype["type"]
        self._logger.debug('Process test result for %s test %s', test_size, suite.uid)

        if (
            suite_prototype["status"] == Status.MISSING
            and not suite_prototype["is_skipped"]
            and not suite.get_comment()
            and node_errors
        ):
            suite_prototype["rich-snippet"] += "\n{}".format("\n".join(node_errors))

        results_root = "build-release"
        replacements = [
            (self._build_root, results_root),
            ('$(BUILD_ROOT)', results_root),
        ]
        import devtools.ya.test.reports as test_reports

        resolver = test_reports.TextTransformer(replacements)

        for k, v in suite.links_map.items():
            self._links_map[resolver.substitute(k)] = v

        if not suite_prototype["is_skipped"]:
            suite.set_work_dir(resolver.substitute(suite.work_dir()))

        entries = []

        for entry_prototype in entry_prototypes:
            entry = {}
            for field in rp.REPORT_ENTRY_COMPLETED_FIELDS:
                if field in entry_prototype:
                    entry[field] = entry_prototype[field]

            entry.update(
                {
                    "owners": ow.find_path_owners(self._owners_list, entry["path"]),
                    "toolchain": results_report.transform_toolchain(
                        self._report_config,
                        entry_prototype["target_platform_descriptor"] or platform_matcher.my_platform(),
                    ),
                }
            )
            # TODO: DEVTOOLS-5005 - stop sending skipped tests to distbs
            requirements = suite._original_requirements
            if requirements:
                entry["requirements"] = requirements

            # for now we change toolchain for all large tests from default-linux-x86_64-relwithdebinfo to
            # default-linux-x86_64-release except tests tagged with ya:relwithdebinfo
            if (
                test_size == test_const.TestSize.Large
                and entry["toolchain"] == "default-linux-x86_64-relwithdebinfo"
                and test_const.YaTestTags.RunWithAsserts not in entry["tags"]
            ):
                entry["toolchain"] = "default-linux-x86_64-release"
            # Add ya:not_autocheck tag to the tests if they are not explicitly marked as exotic
            if (
                entry["toolchain"] == "default-darwin-x86_64-release"
                and entry["status"] == rp.TestStatus.Discovered
                and test_const.YaTestTags.ExoticPlatform not in entry.get("tags", [])
            ):
                entry["tags"] = entry.get("tags", []) + [test_const.YaTestTags.NotAutocheck]

            self._fix_roots(entry, resolver)

            if self._opts.build_results_resource_id is not None:
                _fix_links(entry, results_root, tut.get_log_results_link(self._opts))
            elif (
                self._opts.use_links_in_report
                and (entry != suite_prototype or suite_prototype["links"])
                and not entry_prototype["is_skipped"]
            ):
                _replace_logs_with_links(entry, self._build_root, suite.work_dir(), self._links_map)
            if links:
                if "links" not in entry:
                    entry["links"] = {}
                entry["links"].update(links)

            entries.append(entry)

        toolchain = entries[-1].get("toolchain", None) if len(entries) and entries[-1] else None
        if self.ya_make_progress and toolchain:
            if ci_type_name == YaMakeProgress.STYLE:
                self.ya_make_progress.increment_style_done(toolchain, 1)
            else:
                self.ya_make_progress.increment_test_done(toolchain, test_size, 1)

        self._add_entries(entries)

        def _discard_last_item(m, key, u):
            if key in m and len(m[key]) > 0:
                m[key].discard(u)
                return len(m[key]) == 0
            return False

        if _discard_last_item(self._tests_by_size, test_size, suite.uid):
            self._logger.debug('Fetched all tests results by size %s, close stream', test_size)
            self.finish_tests_report_by_size(test_size)

        if _discard_last_item(self._tests_by_type, ci_type_name, suite.uid):
            self._logger.debug('Fetched all tests results by type %s, close stream', ci_type_name)
            self._finish_report_by_type(ci_type_name)

    def add_tests_results(self, suites, build_errors, node_build_errors_links, report_prototype=None):
        if report_prototype is None:
            report_prototype = defaultdict(list)
        for suite in suites:
            if build_errors:
                target_platform = suite.target_platform_descriptor or platform_matcher.my_platform()
                key = (suite.project_path, target_platform, suite.uid)
                errors = build_errors.get(key)
            else:
                errors = []

            links = {}
            if suite.uid in node_build_errors_links:
                for dep_uid, logs in node_build_errors_links[suite.uid]:
                    for url in logs:
                        links["{}_{}".format(dep_uid, os.path.basename(url))] = [url]

            self._add_test_result(suite, errors, links, report_prototype)

    def add_stage(self, build_stage):
        for report in self._reports:
            report.trace_stage(build_stage)


def prepare_results(test_results, report_prototype, builder, owners_list, configure_errors, results_dir, build_root):
    report = results_report.StoredReport()
    generator = ReportGenerator(
        builder.opts,
        builder.distbuild_graph,
        builder.targets,
        builder.ctx.tests,
        owners_list,
        builder.get_make_files(),
        results_dir,
        build_root,
        [report],
    )
    if not builder.opts.report_skipped_suites_only:
        generator.add_configure_results(configure_errors)
        generator.add_build_results(builder.build_result)

    logger.debug(
        "builder.opts.report_skipped_suites: %s, builder.opts.report_skipped_suites %s",
        builder.opts.report_skipped_suites,
        builder.opts.report_skipped_suites_only,
    )
    if builder.opts.report_skipped_suites or builder.opts.report_skipped_suites_only:
        generator.add_tests_results(
            test_results,
            builder.build_result.build_errors,
            builder.build_result.node_build_errors_links,
            report_prototype,
        )
    else:
        generator.add_tests_results(
            [t for t in test_results if not t.is_skipped()],
            builder.build_result.build_errors,
            builder.build_result.node_build_errors_links,
            report_prototype,
        )

    return report.make_report()
