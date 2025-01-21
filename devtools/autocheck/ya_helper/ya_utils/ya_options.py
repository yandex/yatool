import json
from enum import Enum

import collections
import os
import typing as tp  # noqa: F401
from devtools.autocheck.ya_helper.common import GSIDParts, sorted_items

from .base_options import BaseOptions
from yalibrary import platform_matcher


class YaBaseOptions(BaseOptions):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._handler = None

        self.ya_bin = self._pop('ya_bin')

        self.be_verbose = self._pop('be_verbose')
        self.detailed_args = self._pop('detailed_args')
        self.with_profile = self._pop('with_profile')
        self.error_file = self._pop('error_file')
        self.no_report = self._pop('no_report')
        self.report_events = self.parse_events_filter(self._pop('report_events'))

        self.cache_dir = self._pop('cache_dir')
        self.cache_dir_tools = self._pop('cache_dir_tools')
        self.logs_dir = self._pop('logs_dir')
        self.evlog_path = self._pop('evlog_path')
        self.log_path = self._pop('log_path')
        if self.logs_dir and not self.error_file:
            self.error_file = os.path.join(self.logs_dir, 'error.txt')

        self.token = self._pop('token')
        self.user = self._pop('user')

        self.custom_fetcher = self._pop('custom_fetcher')

    def generate(self, include_new_opts=False):  # type: (bool) -> tp.Tuple[tp.List[str], tp.Dict[str, str]]
        self._check_parameters()
        ya_bin_cmd = self.ya_bin
        if not isinstance(ya_bin_cmd, (list, tuple)):
            ya_bin_cmd = [ya_bin_cmd]
        ya_bin_cmd = list(ya_bin_cmd)

        cmd = ya_bin_cmd + self._generate_pre_handler(include_new_opts)
        cmd += [self._handler] + self._generate_post_handler()

        env = self._generate_env()
        return list(map(str, cmd)), env

    @classmethod
    def parse_events_filter(cls, flt):
        # events can contain None, string or list(string)
        # string can contain values separated by ','
        res = set()
        if flt is not None:
            if isinstance(flt, list) or isinstance(flt, tuple):
                for v in flt:
                    for ev in cls._split_by_column(v):
                        res.add(ev)
            elif isinstance(flt, set):
                res = flt
            else:
                for ev in cls._split_by_column(flt):
                    res.add(ev)
        if not res:
            return None  # avoid clash with ALLOWED_FIELDS check

        return res

    @classmethod
    def _split_by_column(cls, s):
        return [t for t in s.split(',') if t]

    def _generate_pre_handler(self, include_new_opts=False):
        result = []

        if self.be_verbose:
            # TODO: Rename current be_verbose to do_not_rewrite_output_information
            # TODO: set do_not_rewrite_... to enabled by default EVERYWHERE
            # TODO: Make sure that be_verbose disabled everywhere
            # result += ['-v']
            pass
        if self.no_report:
            result += ['--no-report']
        if include_new_opts:
            # new options can be used only after finish support old versions ya-bin without them
            # report_events added 2024-03
            if self.report_events:
                result += ['--report-events', ','.join(sorted(self.report_events))]
        if self.with_profile:
            result += ['--profile']
        if self.error_file:
            result += ['--error-file', self.error_file]
        return result

    def _generate_post_handler(self):
        raise NotImplementedError()

    def _generate_env(self):
        env = {}
        if self.cache_dir:
            env['YA_CACHE_DIR'] = self.cache_dir
        if self.cache_dir_tools:
            env['YA_CACHE_DIR_TOOLS'] = self.cache_dir_tools

        if self.logs_dir:
            env['YA_EVLOG_FILE'] = (
                self.evlog_path if self.evlog_path else os.path.join(self.logs_dir, 'event_log_file.json')
            )
            env['YA_LOG_FILE'] = self.log_path if self.log_path else os.path.join(self.logs_dir, 'log_file.txt')

        if self.token:
            env['YA_TOKEN'] = self.token
            env['ARC_TOKEN'] = self.token
        if self.user:
            env['USER'] = self.user
            env['USERNAME'] = self.user

        if self.custom_fetcher:
            env['YA_CUSTOM_FETCHER'] = self.custom_fetcher

        if self.report_events:
            env['YA_REPORT_EVENTS'] = ','.join(sorted(self.report_events))

        return env


class YaCommandOptions(YaBaseOptions):
    def __init__(self, handler, *cmd, **kwargs):
        super().__init__(**kwargs)
        self._handler = handler
        self._command = list(cmd)

    def _generate_post_handler(self):
        return self._command

    @classmethod
    def _do_merge(cls, self, options, kwargs):
        cmd_options = self
        if isinstance(options, cls):
            cmd_options = options
        return cls(cmd_options._handler, *cmd_options._command, **kwargs)

    @classmethod
    def _choose_class(cls, other_class):
        if other_class is not YaBaseOptions:
            raise TypeError("{} can be only merged with YaBaseOptions".format(cls))

        return super()._choose_class(cls)


class CacheKind(str, Enum):
    parser = 'parser'
    parser_json = 'parser_json'
    parser_deps_json = 'parser_deps_json'

    @staticmethod
    def get_ymake_option(kind):
        if not isinstance(kind, CacheKind):
            kind = CacheKind(kind)
        return CacheKind._ymake_option_by_kind[kind]

    def get_ya_make_option(self):
        return '-x' + CacheKind.get_ymake_option(self)


CacheKind._ymake_option_by_kind = {
    CacheKind.parser: 'CC=f:r,d:n,j:n',
    CacheKind.parser_json: 'CC=f:r,d:n,j:r',
    CacheKind.parser_deps_json: 'CC=f:r,d:r,j:r',
}
# All cache kinds must have the appropriate options
assert {k for k in CacheKind._ymake_option_by_kind.keys()} == {k for k in CacheKind}

DEFAULT_CACHE_KIND = CacheKind.parser


# TODO: Make YaToolOptions
class YaMakeOptions(YaBaseOptions):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._handler = 'make'

        # WARNING: be careful here when adding new options
        # - constructor argument name MUST match field name
        # - field names MUST NOT start with underscore

        self.save_statistics = self._pop('save_statistics')
        self.dump_profile = self._pop('dump_profile')
        self.dump_stages = self._pop('dump_stages')
        self.source_root = self._pop('source_root')
        self.build_type = self._pop('build_type')
        self.keep_build = self._pop('keep_build')
        self.keep_temps = self._pop('keep_temps')
        self.build_root = self._pop('build_root')
        self.output_dir = self._pop('output_dir')
        self.build_results_report = self._pop('build_results_report')
        self.targets = self._pop('targets')
        self.rebuild = self._pop('rebuild')

        self.build_vars = self._pop('build_vars')
        self.host_platform = self._pop('host_platform')
        self.host_platform_flags = self._pop('host_platform_flags')
        self.target_platforms = self._pop('target_platforms')
        self.target_platform_ignore_recurses = self._pop('target_platform_ignore_recurses')
        self.run_tests = self._pop('run_tests')
        self.test_sizes = self._pop('test_sizes')
        self.test_size_timeout = self._pop('test_size_timeout')
        self.test_type_filters = self._pop('test_type_filters')
        self.report_skipped_suites = self._pop('report_skipped_suites')

        self.use_distbuild = self._pop('use_distbuild')

        self.use_distbuild_testing_cluster = self._pop('use_distbuild_testing_cluster')
        self.distbuild_testing_cluster_id = self._pop('distbuild_testing_cluster_id')
        self.distbs_timeout = self._pop('distbs_timeout')
        self.distbs_pool = self._pop('distbs_pool')  # TODO: Rename to distbuild_pool
        self.build_custom_json = self._pop('build_custom_json')
        self.sandboxing = self._pop('sandboxing')
        self.strict_inputs = self._pop('strict_inputs')
        self.custom_context = self._pop('custom_context')
        self.dump_json_graph = self._pop('dump_json_graph')
        self.save_graph_to = self._pop('save_graph_to')
        self.save_context_to = self._pop('save_context_to')
        self.dump_distbuild_rpc_log = self._pop('dump_distbuild_rpc_log')
        self.dump_distbuild_result = self._pop('dump_distbuild_result')
        self.dump_distbuild_graph = self._pop('dump_distbuild_graph')
        self.dist_priority = self._pop('dist_priority')
        if self.dist_priority:
            self.dist_priority = int(self.dist_priority)
        self.graph_gen_dist_priority = self._pop('graph_gen_dist_priority')
        if self.graph_gen_dist_priority:
            self.graph_gen_dist_priority = int(self.graph_gen_dist_priority)  # TODO: Change in sandbox

        self.build_threads = self._pop('build_threads')

        self.misc_build_info_dir = self._pop('misc_build_info_dir')

        self.revision = self._pop('revision')
        if self.revision:
            self.revision = int(self.revision)
        self.svn_url = self._pop('svn_url')

        self.make_context_on_distbuild = self._pop('make_context_on_distbuild')
        self.make_context_only = self._pop('make_context_only')
        self.make_context_on_distbuild_requirements = self._pop('make_context_on_distbuild_requirements')
        self.make_context_on_distbuild_timeout = self._pop('make_context_on_distbuild_timeout')
        self.make_context = self._pop('make_context')
        self.use_svn_repository_for_dist_build = self._pop('use_svn_repository_for_dist_build')
        self.download_artifacts = self._pop('download_artifacts')
        self.output_only_tests = self._pop('output_only_tests')
        self.strip_idle_build_results = self._pop('strip_idle_build_results')

        self.do_not_download_tests_results = self._pop('do_not_download_tests_results')
        self.backup_tests_results = self._pop('backup_tests_results')

        self.json_prefix = self._pop('json_prefix')
        self.coordinators_filter = self._pop('coordinators_filter')  # TODO: Remove
        self.cache_namespace = self._pop('cache_namespace')
        self.no_tests_on_distbuild = self._pop('no_tests_on_distbuild')
        self.patch_spec = self._pop('patch_spec')
        self.report_config = self._pop('report_config')

        self.testenv_report_dir = self._pop('testenv_report_dir')
        self.stream_partition = self._pop('stream_partition')
        self.streaming_url = self._pop('streaming_url')
        self.streaming_id = self._pop('streaming_id')
        self.streaming_task_id = self._pop('streaming_task_id')
        self.streaming_stage_namespace = self._pop('streaming_stage_namespace')
        self.keep_alive_streams = self._pop('keep_alive_streams')
        self.keep_alive_all_streams = self._pop('keep_alive_all_streams')

        self.report_to_ci = self._pop('report_to_ci')
        self.ci_topic = self._pop('ci_topic')
        self.ci_source_id = self._pop('ci_source_id')
        self.ci_check_id = self._pop('ci_check_id')
        self.ci_check_type = self._pop('ci_check_type')
        self.ci_iteration_number = self._pop('ci_iteration_number')
        self.ci_task_id_string = self._pop('ci_task_id_string')
        self.ci_logbroker_partition_group = self._pop('ci_logbroker_partition_group')
        self.ci_use_ydb_topic_client = self._pop('ci_use_ydb_topic_client')

        self.report_skipped_suites_only = self._pop('report_skipped_suites_only')
        self.build_graph_cache_autocheck_params = self._pop('build_graph_cache_autocheck_params')
        self.build_graph_result_dir = self._pop('build_graph_result_dir')
        self.build_graph_source_root_pattern = self._pop('build_graph_source_root_pattern')

        self.custom_targets_list = self._pop('custom_targets_list')
        self.cache_tests = self._pop('cache_tests')
        self.tests_retries = self._pop('tests_retries')
        self.toolchain_transforms = self._pop('toolchain_transforms')

        self.dont_merge_split_tests = self._pop('dont_merge_split_tests')
        self.remove_result_node = self._pop('remove_result_node')
        self.strip_packages_from_results = self._pop('strip_packages_from_results')

        self.build_threads_for_distbuild = self._pop('build_threads_for_distbuild')

        self.profile_to = self._pop('profile_to')

        self.skip_test_console_report = self._pop('skip_test_console_report')
        self.ignore_nodes_exit_code = self._pop('ignore_nodes_exit_code')
        self.warning_mode = self._pop('warning_mode')

        self.add_modules_to_results = self._pop('add_modules_to_results')
        self.do_not_output_stderrs = self._pop('do_not_output_stderrs')
        self.add_changed_ok_configures = self._pop('add_changed_ok_configures')

        # caches
        self.ymake_cache_kind = self._pop('ymake_cache_kind')
        use_ymake_cache = self._pop('use_ymake_cache')
        if use_ymake_cache is True:
            use_ymake_cache = DEFAULT_CACHE_KIND
            self.logger.info("Set default cache kind, which is %s", use_ymake_cache)
        self.use_ymake_cache = use_ymake_cache
        self.use_ymake_minimal_cache = self._pop('use_ymake_minimal_cache')
        self.autocheck_params = self._pop('autocheck_params')
        self.use_imprint_cache = self._pop('use_imprint_cache')
        self.trust_cache_fs = self._pop('trust_cache_fs')
        self.build_graph_cache_archive = self._pop('build_graph_cache_archive')
        self.build_graph_cache_heater = self._pop('build_graph_cache_heater')

        # yt_store
        self.no_yt_store = self._pop('no_yt_store')
        self.yt_proxy = self._pop('yt_proxy')
        self.yt_dir = self._pop('yt_dir')
        self.yt_token_path = self._pop('yt_token_path')

        # env
        self.no_gen_renamed_results = self._pop('no_gen_renamed_results')
        self.toolchain = self._pop('toolchain')  # TODO: ?
        self.no_respawn = self._pop('no_respawn')
        self.test_disable_flake8_migrations = self._pop('test_disable_flake8_migrations')
        self.dont_use_tokens_in_graph = self._pop('dont_use_tokens_in_graph')
        self.autocheck = self._pop('autocheck')
        self.use_new_distbuild_client = self._pop('use_new_distbuild_client')
        self.use_arcc_in_distbuild = self._pop('use_arcc_in_distbuild')
        self.content_uids = self._pop('content_uids')

        self.make_context_on_distbuild_only = self._pop('make_context_on_distbuild_only')
        self.high_priority_mds_read = self._pop('high_priority_mds_read')
        self.merge_split_tests = self._pop('merge_split_tests')

        self.graph_distbuild_pool = self._pop('graph_distbuild_pool')
        self.graph_coordinators_filter = self._pop('graph_coordinators_filter')
        self.arc_url_as_working_copy_in_distbuild = self._pop('arc_url_as_working_copy_in_distbuild')

        self.distbuild_statistic_resource_id = self._pop('distbuild_statistic_resource_id')

        self.gsid = self._pop('gsid')
        if self.gsid is not None:
            if isinstance(self.gsid, str):
                self.gsid = GSIDParts(self.gsid)
            assert isinstance(self.gsid, GSIDParts)

        self.dump_failed_node_info_to_evlog = self._pop('dump_failed_node_info_to_evlog')

        self.no_ymake_retry = self._pop('no_ymake_retry')

        self.ci_logbroker_token = self._pop('ci_logbroker_token')
        self.ya_tc = self._pop('ya_tc')
        self.ya_ac = self._pop('ya_ac')
        self.evlog_node_stat = self._pop('evlog_node_stat')
        self.compress_ymake_output = self._pop('compress_ymake_output')

        self.cache_size = self._pop("cache_size")
        self.tools_cache_size = self._pop("tools_cache_size")

        self.ignore_configure_errors = self._pop('ignore_configure_errors')

        self.no_prefetch = self._pop('no_prefetch')

        self.ymake_tool_servermode = self._pop('ymake_tool_servermode')
        self.store_links_in_memory = self._pop('store_links_in_memory')

    def _generate_post_handler(self):
        result = []
        if self.use_distbuild:
            result += self._distbuild_options()
        else:
            result += self._local_build_options()

        if self.targets:
            result += ['-C', ';'.join(self.targets)]
        if self.build_vars:
            result += ['-D{var}'.format(var=var) for var in self.build_vars]
        if self.host_platform:
            result += ['--host-platform', self.host_platform]
        if self.host_platform_flags:
            result += sum([['--host-platform-flag', var] for var in self.host_platform_flags], [])

        if self.custom_targets_list:
            result += self._make_custom_targets_options()
        elif self.target_platforms:
            result += sum([platform_matcher.make_platform_params(pl) for pl in self.target_platforms], [])

        if self.sandboxing:
            result += ['--sandboxing']
        if self.strict_inputs:
            result += ['--strict-inputs']
        if self.run_tests:
            result += ['--run-tests']
        if self.report_skipped_suites:
            result += ['--report-skipped-suites']
        if self.run_tests or '--target-platform-tests' in result:
            result += ['--tests-retries', str(self.tests_retries)]
        if self.test_sizes:
            for size in self.test_sizes:
                result += ['--test-size', size]
        if self.test_size_timeout:
            result.append('--test-size-timeout')
            result += [
                '{size}={timeout}'.format(size=key, timeout=value) for key, value in self.test_size_timeout.items()
            ]
        if self.test_type_filters:
            for type_filter in self.test_type_filters:
                result += ['--test-type', type_filter]
        if self.build_type:
            result += ['--build', self.build_type.lower()]
        if self.source_root:
            result += ['--source-root', self.source_root]
        if self.output_dir:
            result += ['--output', self.output_dir]
        if self.build_root:
            result += ['--build-dir', self.build_root]
        if self.dump_profile:
            result += ['--profile', self.dump_profile]
        if self.dump_stages:
            result += ['--stages', self.dump_stages]
        if self.testenv_report_dir:
            result += ['--testenv-report-dir', self.testenv_report_dir]
        if self.streaming_url:
            result += ['--streaming-report-url', self.streaming_url]
        if self.streaming_id:
            result += ['--streaming-report-id', str(self.streaming_id)]
        if self.streaming_task_id:
            result += ['--streaming-task-id', str(self.streaming_task_id)]
        if self.stream_partition is not None:
            result += ['--stream-partition', str(self.stream_partition)]
        if self.keep_alive_streams:
            result += ['--keep-alive-streams', self.keep_alive_streams]
        if self.keep_alive_all_streams:
            result += ['--keep-alive-all-streams']
        if self.report_to_ci:
            result += ['--report-to-ci']
        if self.ci_topic:
            result += ['--ci-topic', self.ci_topic]
        if self.ci_source_id:
            result += ['--ci-source-id', self.ci_source_id]
        if self.ci_check_id:
            result += ['--ci-check-id', self.ci_check_id]
        if self.ci_check_type:
            result += ['--ci-check-type', self.ci_check_type]
        if self.ci_iteration_number:
            result += ['--ci-iteration-number', str(self.ci_iteration_number)]
        if self.ci_task_id_string:
            result += ['--ci-task-id-string', self.ci_task_id_string]
        if self.ci_logbroker_partition_group:
            result += ['--ci-logbroker-partition-group', str(self.ci_logbroker_partition_group)]
        if self.misc_build_info_dir:
            result += ['--misc-build-info-dir', self.misc_build_info_dir]
        if self.revision:
            result += ['--revision', str(self.revision)]
        if self.svn_url:
            result += ['--svn-url', self.svn_url]
        if self.make_context_on_distbuild or self.make_context_only:
            result += ['--make-context-on-distbuild'] if self.make_context_on_distbuild else ['--make-context-only']
            if self.make_context_on_distbuild_requirements:
                result += ['--make-context-on-distbuild-reqs', self.make_context_on_distbuild_requirements]
            if self.make_context_on_distbuild_timeout:
                result += ['--make-context-on-distbuild-timeout', self.make_context_on_distbuild_timeout]
        if self.make_context:
            result += ['--make-context']
        if self.save_statistics:
            result += ['--stat', '--stat-dir', os.path.join(self.misc_build_info_dir, 'build-statistics')]
        if self.rebuild:
            result += ['--rebuild']
        if self.be_verbose:
            result += ['-T']
        if self.keep_build:
            result += ['-k']
        if self.keep_temps:
            result += ['--keep-temps']
        if self.profile_to:
            result += ['--profile-to', 'ya.prof']
        if self.do_not_download_tests_results:
            for fn in [
                'results_merge.log',
                'run_test.log',
                'testing_out_stuff.tar',
                'testing_out_stuff.tar.zstd',
                'yt_run_test.tar',
            ]:
                result += ['--save-links-for', fn]
            result += ['--use-links-in-report']
        if self.backup_tests_results:
            result += ['--backup-test-results']
        if self.report_config:
            result += ['--report-config', self.report_config]
        if self.report_skipped_suites_only:
            result += ['--report-skipped-suites-only']
        if self.dump_json_graph:
            result += ['--dump-json-graph']
        if self.save_graph_to:
            result += ['--save-graph-to', self.save_graph_to]
        if self.save_context_to:
            result += ['--save-context-to', self.save_context_to]
        if self.build_custom_json:
            result += ['--build-custom-json', self.build_custom_json]
        if self.custom_context:
            result += ['--custom-context', self.custom_context]
        if self.build_results_report:
            result += ['--build-results-report', self.build_results_report]
        if self.add_modules_to_results:
            result += ['--add-modules-to-results']
        if self.do_not_output_stderrs:
            result += ['--do-not-output-stderrs']
        result += ['--no-src-links']

        if self.ymake_cache_kind:
            params = {}

            if not self.autocheck_params:
                raise ValueError("Please set `autocheck_params` to use ymake caches")
            params.update(self.autocheck_params)

            ymake_cache_kind = CacheKind(self.ymake_cache_kind)
            params['ymake_cache_kind'] = ymake_cache_kind.value

            autocheck_params = json.dumps(params)

            result += ['--build-graph-autocheck-params', autocheck_params]

        if self.use_ymake_cache:
            use_ymake_cache = CacheKind(self.use_ymake_cache)
            cache_option = use_ymake_cache.get_ya_make_option()
            result.append(cache_option)

        if self.use_imprint_cache:
            result.append('--cache-fs-read')

        # TODO: Temporary for compatibility, will be removed soon
        if self.build_graph_cache_autocheck_params:
            self.logger.warning(
                "Parameter `build_graph_cache_autocheck_params` is deprecated; use "
                "`ymake_cache_kind`, `use_ymake_cache`, `use_imprint_cache`"
            )
            result += self.build_graph_cache_autocheck_params

        if self.build_graph_result_dir:
            result += ['--build-graph-result-dir', self.build_graph_result_dir]
        if self.build_graph_source_root_pattern:
            result += ['--build-graph-source-root-pattern', self.build_graph_source_root_pattern]
        if self.remove_result_node:
            result += ['--remove-result-node']
        if self.add_changed_ok_configures:
            result += ['--add-changed-ok-configures']
        # TODO: check if this distbuild_options
        if self.dont_merge_split_tests:
            result += ['--dont-merge-split-tests']
        if self.strip_packages_from_results:
            result += ['--strip-packages-from-results']
        if self.build_graph_cache_archive:
            result += ['--build-graph-cache-archive', self.build_graph_cache_archive]
        if self.build_graph_cache_heater:
            result += ['--build-graph-cache-heater'] + self.build_graph_cache_heater

        if self.no_yt_store:
            result += ['--no-yt-store']
        if self.yt_proxy:
            result += ['--yt-proxy', self.yt_proxy]
        if self.yt_dir:
            result += ['--yt-dir', self.yt_dir]
        if self.yt_token_path:
            result += ['--yt-token-path', self.yt_token_path]

        return result

    def _common_options(self):
        result = []
        if self.skip_test_console_report:
            result += ['--skip-test-console-report']
        if self.ignore_nodes_exit_code:
            result += ['--ignore-nodes-exit-code']
        if self.warning_mode:
            result += ['--warning-mode', ';'.join(self.warning_mode)]
        if self.cache_tests:
            result += ['--cache-tests']
        if self.output_only_tests:
            result += ['--output-only-tests']
        if self.patch_spec:
            result += ['--apply-patch', self.patch_spec]

        return result

    def _distbuild_options(self):
        result = ['--dist'] + self._common_options()

        if self.download_artifacts:
            result += ['--download-artifacts']
        if self.use_svn_repository_for_dist_build:
            result += ['--use-svn-repository-for-dist-build']
        if self.dump_distbuild_result:
            result += ['--dump-distbuild-result', self.dump_distbuild_result]
        if self.dump_distbuild_graph:
            result += ['--dump-distbuild-graph', self.dump_distbuild_graph]
        if self.use_distbuild_testing_cluster:
            result += ['--testing']
        if self.distbuild_testing_cluster_id:
            result += ['--testing-cluster-id', str(self.distbuild_testing_cluster_id)]
        if self.distbs_timeout:
            result += ['--build-time', str(self.distbs_timeout)]
        if self.distbs_pool:
            result += ['--distbuild-pool', str(self.distbs_pool)]
        if self.json_prefix:
            result += ['--json-prefix', str(self.json_prefix)]
        if self.coordinators_filter:
            result += ['--coordinators-filter', self.coordinators_filter]
        if self.cache_namespace:
            result += ['--cache-namespace', self.cache_namespace]
        if self.no_tests_on_distbuild:
            result += ['--no-tests-on-distbuild']
        if self.dump_distbuild_rpc_log:
            result += ['--dump-distbuild-rpc-log', self.dump_distbuild_rpc_log]
        if self.build_threads_for_distbuild is not None:
            result += ['-j', self.build_threads_for_distbuild]
        if self.graph_gen_dist_priority is not None:
            result += ['--graph-gen-dist-priority', str(self.graph_gen_dist_priority)]
        if self.evlog_node_stat:
            result += ['--evlog-node-stat']
        if self.dist_priority is not None:
            result += ['--dist-priority', str(self.dist_priority)]
        return result

    def _local_build_options(self):
        result = self._common_options()
        if self.build_threads is not None:
            result += ['-j', '%d' % self.build_threads]

        # Need for meta graph generation
        if self.graph_gen_dist_priority is not None:
            result += ['--graph-gen-dist-priority', str(self.graph_gen_dist_priority)]

        return result

    def _make_custom_targets_options(self):
        # Output must store the order
        by_toolchain = collections.defaultdict(lambda: collections.defaultdict(list))
        for target, toolchain, test_types in self.custom_targets_list:
            for test_type in test_types:
                by_toolchain[toolchain][test_type].append(target)

        res = []

        toolchain_aliases_order = list(
            map(
                lambda toolchain: platform_matcher.get_target_platform_alias(toolchain, self.toolchain_transforms),
                self.target_platforms,
            )
        )

        _extra_toolchains = set(by_toolchain.keys()) - set(toolchain_aliases_order)
        assert not _extra_toolchains, "Found extra toolchains: {}".format(_extra_toolchains)

        for toolchain in toolchain_aliases_order:
            if toolchain not in by_toolchain:
                continue
            platform_params = platform_matcher.transform_toolchain(
                toolchain, self.target_platforms, self.toolchain_transforms
            )
            if platform_params:
                if self.target_platform_ignore_recurses:
                    platform_params.append('--target-platform-ignore-recurses')

                for test_type, targets in sorted_items(by_toolchain[toolchain]):
                    platform_params_copy = list(platform_params)
                    if test_type is not None:
                        platform_params_copy += ['--target-platform-test-type', test_type]
                    for target in targets:
                        platform_params_copy += ['--target-platform-target', target]
                    res += platform_params_copy
            else:
                self._logger.warning('Platform for {} alias was not found'.format(toolchain))
        return res

    def _bool_to_env(self, b):
        if isinstance(b, bool):
            return '1' if b else '0'
        return str(b)

    def _generate_env(self):
        env = super()._generate_env()
        if self.dist_priority:
            env['YA_GRAPH_GEN_DIST_PRIORITY'] = str(self.dist_priority)
        if self.use_distbuild:
            if self.distbuild_statistic_resource_id:
                env['YA_NODES_STAT_RESOURCE'] = 'sbr:{}'.format(self.distbuild_statistic_resource_id)
                env['YA_DUMP_GRAPH_EXECUTION_COST'] = 'execution_cost.json'

        if self.toolchain is not None:
            env['TOOLCHAIN'] = self.toolchain

        # TODO: Understand how this environments works and unify

        if self.graph_distbuild_pool is not None:
            env['YA_GRAPH_DISTBUILD_POOL'] = self.graph_distbuild_pool
        if self.graph_coordinators_filter is not None:
            env['YA_GRAPH_COORDINATORS_FILTER'] = self.graph_coordinators_filter
        if self.arc_url_as_working_copy_in_distbuild is not None:
            env['YA_ARC_URL_AS_WORKING_COPY_IN_DISTBUILD'] = self.arc_url_as_working_copy_in_distbuild

        if self.strip_idle_build_results is not None:
            env['YA_STRIP_IDLE_BUILD_RESULTS'] = self._bool_to_env(self.strip_idle_build_results)
        if self.merge_split_tests is not None:
            env['YA_MERGE_SPLIT_TESTS'] = self._bool_to_env(self.merge_split_tests)
        if self.test_disable_flake8_migrations is not None:
            env['YA_TEST_DISABLE_FLAKE8_MIGRATIONS'] = self._bool_to_env(self.test_disable_flake8_migrations)
        if self.content_uids is not None:
            env['YA_USE_CONTENT_UIDS'] = self._bool_to_env(self.content_uids)

        if self.no_gen_renamed_results:
            env['YA_NO_GEN_RENAMED_RESULTS'] = '1'
        if self.no_respawn:
            env['YA_NO_RESPAWN'] = '1'
        if self.dont_use_tokens_in_graph:
            env['YA_DONT_USE_TOKENS_IN_GRAPH'] = '1'
        if self.autocheck:
            env['AUTOCHECK'] = '1'
        if self.use_new_distbuild_client:
            env['YA_USE_NEW_DISTBUILD_CLIENT'] = '1'
        if self.use_arcc_in_distbuild:
            env['YA_USE_ARCC_IN_DISTBUILD'] = '1'
        if self.make_context_on_distbuild_only:
            env['YA_MAKE_CONTEXT_ON_DISTBUILD_ONLY'] = '1'
        if self.high_priority_mds_read:
            env['YA_HIGH_PRIORITY_MDS_READ'] = '1'
        if self.trust_cache_fs:
            if self.patch_spec:
                self._logger.warning(
                    'Can not use option for completely trust cache fs (--build-graph-cache-trust-cl) if has patch (instead change-list)'
                )
            else:
                env['YA_BUILD_GRAPH_CACHE_TRUST_CL'] = '1'

        if self.gsid is not None:
            env['GSID'] = str(self.gsid)

        if self.dump_failed_node_info_to_evlog:
            env['YA_EVLOG_DUMP_FAILED_NODE_INFO'] = '1'

        if self.no_ymake_retry:
            env['YA_NO_YMAKE_RETRY'] = '1'

        # Is used in meta graph generation as alternative to -x<cache option>
        if self.make_context_only and (self.use_ymake_cache or self.use_ymake_minimal_cache):
            opts = {}
            if self.use_ymake_cache:
                opts['normal'] = CacheKind.get_ymake_option(self.use_ymake_cache)
            if self.use_ymake_minimal_cache:
                opts['minimal'] = CacheKind.get_ymake_option(self.use_ymake_minimal_cache)
            env['YA_BUILD_GRAPH_USE_YMAKE_CACHE_PARAMS'] = json.dumps(opts)

        if self.ci_logbroker_token:
            env['YA_CI_LOGBROKER_TOKEN'] = self.ci_logbroker_token

        if self.streaming_stage_namespace:
            env['YA_STREAMING_STAGE_NAMESPACE'] = self.streaming_stage_namespace

        if self.ya_tc is not None:
            env['YA_TC'] = self._bool_to_env(self.ya_tc)
        if self.ya_ac is not None:
            env['YA_AC'] = self._bool_to_env(self.ya_ac)
        if self.compress_ymake_output:
            env['YA_COMPRESS_YMAKE_OUTPUT'] = '1'

        if self.ci_use_ydb_topic_client:
            env['YA_CI_USE_YDB_TOPIC_CLIENT'] = '1'

        if self.cache_size:
            env['YA_CACHE_SIZE'] = str(self.cache_size)
        if self.tools_cache_size:
            env['YA_TOOLS_CACHE_SIZE'] = str(self.tools_cache_size)

        if self.ignore_configure_errors:
            env['YA_IGNORE_CONFIGURE_ERRORS'] = '1'

        if self.no_prefetch:
            env['YA_PREFETCH'] = '0'

        if self.ymake_tool_servermode is not None:
            env['YA_YMAKE_TOOL_SERVERMODE'] = self._bool_to_env(self.ymake_tool_servermode)

        if self.store_links_in_memory is not None:
            env['YA_STORE_LINKS_IN_MEMORY'] = self._bool_to_env(self.store_links_in_memory)

        return env


class YaOnlyAllowedOptions(YaMakeOptions):
    # TODO: Autodiscovery ALLOWED_FIELDS in super-classes
    # TODO: When you need to implement, for example YaToolOptions, make this mixin
    ALLOWED_FIELDS = set()

    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            if k not in self.ALLOWED_FIELDS:
                raise KeyError(
                    "Parameter name {} not allowed in class {}. Try one of: {}".format(
                        k, self.__class__.__name__, self.ALLOWED_FIELDS
                    )
                )

        super().__init__(**kwargs)

    @classmethod
    def _choose_class(cls, other_class):
        if other_class is YaBaseOptions:
            return YaMakeOptions

        if other_class is YaCommandOptions:
            return other_class._choose_class(cls)

        if not issubclass(other_class, YaOnlyAllowedOptions):
            return other_class

        return super()._choose_class(other_class)
