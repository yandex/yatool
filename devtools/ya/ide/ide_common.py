from __future__ import absolute_import
import collections
import copy
import itertools
import exts.yjson as json
import logging
import os
import re
import sys
import subprocess
import traceback

import six

import app
import build.build_facade
import build.build_opts
import build.gen_plan2
import build.genconf
import build.graph_path
import build.makelist
import build.targets_deref
import core.common_opts
import core.config
import core.yarg
import exts.filelock
import exts.fs
import exts.func
import exts.os2
import exts.path2
import exts.process
import yalibrary.display
import yalibrary.platform_matcher
from six.moves import filter

logger = logging.getLogger(__name__)

DEFAULT_PROJECT_TITLE = 'arcadia'
JOINT_TARGET_NAME = 'all_arcadia'
CODEGEN_TARGET_NAME = 'all_arcadia_codegen'
IDE_YMAKE_BUILD_TYPE = 'nobuild'
SYNC_LIST_FILE_NAME = 'sync_list'
JOINT_TARGET_REMOTE_NAME = 'all_remote_arcadia'
DUMMY_CPP_PROJECT_ARCADIA_PATH = 'devtools/dummy_arcadia/models/modules/program'
DEFAULT_REMOTE_CACHE_DIR = '.ya.ideremote'
REMOTE_OUTPUT_SUBDIR = 'output'
REMOTE_SOURCE_SUBDIR = 'source'
REMOTE_BUILD_SUBDIR = 'build'

RE_PROJECT_TITLE = re.compile('[a-z0-9_]+', re.I)
RE_ESCAPE_IN_UNQUOTED = re.compile(r'[ ()#"\\]')


class FakeAppCtx(object):
    display = yalibrary.display.DevNullDisplay()


class SSHProxy(object):
    SSH = 'ssh'

    def __init__(self, host, ssh_args=None):
        self.host = host
        self.ssh_args = ssh_args or []

    def is_host_up(self):
        import socket

        try:
            socket.getaddrinfo(self.host, None)
            return True
        except socket.error:
            return False

    def is_exec(self, path):
        return exts.process.popen([self.SSH] + self.ssh_args + [self.host, 'test', '-x', path]).wait() == 0

    def is_dir(self, path):
        return exts.process.popen([self.SSH] + self.ssh_args + [self.host, 'test', '-d', path]).wait() == 0

    def mkdir_p(self, path):
        exts.process.run_process(self.SSH, self.ssh_args + [self.host, 'mkdir', '-p', path], check=True)

    def ya_clone(self, ya_path, path):
        exts.process.run_process(
            self.SSH, self.ssh_args + [self.host, ya_path, 'clone', path], check=True, pipe_stdout=False
        )

    @staticmethod
    def _remote_ya_path(arc_path):
        return os.path.join(arc_path, 'ya')

    def checkout(self, arc_path, rel_targets):
        ya_opts = ['make', '-j0', '--checkout']
        exts.process.run_process(
            self.SSH,
            self.ssh_args
            + [self.host, self._remote_ya_path(arc_path)]
            + ya_opts
            + [os.path.join(arc_path, p) for p in rel_targets],
            check=True,
            pipe_stdout=False,
        )

    def get_real_path(self, path):
        return exts.process.run_process(self.SSH, self.ssh_args + [self.host, 'readlink', '-n', '-f', path], check=True)

    def get_free_port(self, remote_cache):
        arc_root = os.path.join(remote_cache, REMOTE_SOURCE_SUBDIR)
        return exts.process.run_process(
            self.SSH,
            self.ssh_args + [self.host, self._remote_ya_path(arc_root), 'remote_gdb', '--find-port'],
            check=True,
        ).strip()

    def start_gdbserver(self, remote_cache, port):
        arc_root = os.path.join(remote_cache, REMOTE_SOURCE_SUBDIR)
        pidfile_path = get_pidfile_path(remote_cache, port)
        self.mkdir_p(os.path.dirname(pidfile_path))
        cmd = '{{ {0} remote_gdb --remote-cache {1} --port {2} --start-server </dev/null >/dev/null 2>/dev/null & }} ; echo $! > {3}'.format(
            self._remote_ya_path(arc_root), remote_cache, port, pidfile_path
        )
        exts.process.run_process(self.SSH, self.ssh_args + [self.host, cmd], check=True)

    def stop_gdbserver(self, remote_cache, port):
        arc_root = os.path.join(remote_cache, REMOTE_SOURCE_SUBDIR)
        exts.process.run_process(
            self.SSH,
            self.ssh_args
            + [
                self.host,
                self._remote_ya_path(arc_root),
                'remote_gdb',
                '--remote-cache',
                remote_cache,
                '--port',
                port,
                '--stop-server',
            ],
            check=True,
        )

    def touch(self, path, opts=None):
        exts.process.run_process(self.SSH, self.ssh_args + [self.host, 'touch'] + (opts or []) + [path], check=True)


def ide_minimal_opts(targets_free=False, prefetch=False):
    return [
        build.build_opts.BuildTargetsOptions(with_free=targets_free),
        build.build_opts.ArcPrefetchOptions(prefetch=prefetch),
        core.common_opts.ShowHelpOptions(),
        core.common_opts.DumpDebugOptions(),
        core.common_opts.AuthOptions(),
        core.common_opts.OutputStyleOptions(),
    ]


def ide_opts(targets_free=True):
    return ide_minimal_opts(targets_free=targets_free) + [
        IdeProjectTitleOptions(),
        IdeProjectOutputOptions(),
        IdeProjectJavaFixOptions(),
    ]


def ide_via_ya_make_opts(targets_free=True):
    return ide_opts(targets_free=targets_free) + [
        IdeYaMakeOptions(),
        YaExtraArgsOptions(),
        build.build_opts.CustomFetcherOptions(),
        build.build_opts.SandboxAuthOptions(),
        build.build_opts.ToolsOptions(),
    ]


class IdeProjectInstallOptions(core.yarg.Options):
    def __init__(self):
        self.install = False

    def consumer(self):
        return [
            core.yarg.ArgConsumer(
                ['--install'],
                help='Install settings for Ide',
                hook=core.yarg.SetConstValueHook('install', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class IdeProjectVersionOptions(core.yarg.Options):
    def __init__(self, **kwargs):
        self.project_version = kwargs.get('default_version')
        self.valid_project_versions = kwargs.get('valid_versions')

    def consumer(self):
        return [
            core.yarg.ArgConsumer(
                ['-V', '--project-version'],
                help='IDE version'
                + (
                    ': {}'.format(', '.join(str(x) for x in self.valid_project_versions))
                    if self.valid_project_versions
                    else ''
                ),
                hook=core.yarg.SetValueHook('project_version'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]

    def postprocess(self):
        if self.valid_project_versions and self.project_version not in self.valid_project_versions:
            raise core.yarg.ArgsValidatingException(
                'Unsupported IDE version \'{}\', valid versions: {}'.format(
                    self.project_version, ', '.join(self.valid_project_versions)
                )
            )


class IdeProjectTitleOptions(core.yarg.Options):
    def __init__(self, consume_free_args=False):
        self._consume_free_args = consume_free_args
        self.project_title = DEFAULT_PROJECT_TITLE
        self.dirname_as_project_title = False

    def consumer(self):
        consumers = [
            core.yarg.ArgConsumer(
                ['-T', '--project-title'],
                help='Custom IDE project title',
                hook=core.yarg.SetValueHook('project_title'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--dirname-as-project-title'],
                help='Use cwd dirname as default IDE project title',
                hook=core.yarg.SetConstValueHook('dirname_as_project_title', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]
        if self._consume_free_args:
            consumers.append(
                core.yarg.FreeArgConsumer(help='project_title', hook=core.yarg.SetValueHook('project_title'))
            )
        return consumers

    def postprocess(self):
        if self.dirname_as_project_title:
            self.project_title = os.path.basename(os.getcwd())
        if self.project_title is not None and not RE_PROJECT_TITLE.match(self.project_title):
            if self.dirname_as_project_title:
                raise core.yarg.ArgsValidatingException(
                    "You cwd directory name '{}' do not match pattern '{}'. "
                    "Please specify project name using -T or --project-title option".format(
                        self.project_title, RE_PROJECT_TITLE.pattern
                    )
                )
            raise core.yarg.ArgsValidatingException('Invalid IDE project title \'{}\''.format(self.project_title))


class IdeProjectJavaFixOptions(core.yarg.Options):
    def __init__(self):
        self.java_fix = True

    @staticmethod
    def consumer():
        return core.yarg.ArgConsumer(
            ['--disable-java'],
            hook=core.yarg.SetConstValueHook('java_fix', False),
            group=core.yarg.ADVANCED_OPT_GROUP,
            visible=False,
        )


class IdeProjectOutputOptions(core.yarg.Options):
    def __init__(self):
        self.project_output = None

    def consumer(self):
        return [
            core.yarg.ArgConsumer(
                ['-P', '--project-output'],
                help='Custom IDE project output directory',
                hook=core.yarg.SetValueHook('project_output'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]

    def postprocess(self):
        if self.project_output:
            self.project_output = exts.path2.abspath(self.project_output, expand_user=True)


class YaExtraArgsOptions(core.yarg.Options):
    def __init__(self):
        self.ya_make_extra = []

    def consumer(self):
        return [
            core.yarg.ArgConsumer(
                ['--make-args'],
                help='Extra ya make arguments',
                hook=core.yarg.SetAppendHook('ya_make_extra'),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class IdeYaMakeOptions(core.yarg.Options):
    def __init__(self):
        self.ya_make_symlinks = False
        self.download_artifacts = False
        self.use_distbuild = False

    def consumer(self):
        consumers = [
            core.yarg.ArgConsumer(
                ['--make-src-links'],
                help='Create ya make symlinks in source tree',
                hook=core.yarg.SetConstValueHook('ya_make_symlinks', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--dist'],
                help='Use distbuild',
                hook=core.yarg.SetConstValueHook('use_distbuild', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]
        return consumers

    def postprocess(self):
        if self.use_distbuild:
            self.download_artifacts = True


class RemoteOptions(core.yarg.Options):
    def __init__(self, require_remote=False):
        self.require_remote = require_remote
        self.remote_host = None
        self.remote_cache_path = DEFAULT_REMOTE_CACHE_DIR

    @staticmethod
    def consumer():
        return core.yarg.ArgConsumer(
            ['-H', '--host'],
            help='Host machine address',
            hook=core.yarg.SetValueHook('remote_host'),
            group=core.yarg.BULLET_PROOF_OPT_GROUP,
        ) + core.yarg.ArgConsumer(
            ['--remote-cache'],
            help='Path to the service directory on the remote machine',
            hook=core.yarg.SetValueHook('remote_cache_path'),
            group=core.yarg.ADVANCED_OPT_GROUP,
        )

    def postprocess(self):
        if self.require_remote and not self.remote_host:
            raise core.yarg.ArgsValidatingException('Remote host must be specified with \'--host\' option')


class IdeRemoteOptions(core.yarg.Options):
    def __init__(self, require_remote=False):
        self.require_remote = require_remote
        self.remote_clean_output = True
        self.rsync_upload_args = '-avz --copy-unsafe-links'
        self.rsync_down_args = '-rpthvz --copy-unsafe-links'
        self.in_build_get_source = True
        self.forward_key = False
        self.remote_ya_path = None
        self.remote_prepare_env = True
        self.run = False

    @staticmethod
    def consumer():
        return (
            core.yarg.ArgConsumer(
                ['--remote-keep-output'],
                help='Don\'t clean remote output dir after build',
                hook=core.yarg.SetConstValueHook('remote_clean_output', False),
                group=core.yarg.ADVANCED_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--rsync_up_args'],
                help='Rsync arguments for uploading sources',
                hook=core.yarg.SetValueHook('rsync_upload_args'),
                group=core.yarg.DEVELOPERS_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--rsync_down_args'],
                help='Rsync arguments for downloading build results',
                hook=core.yarg.SetValueHook('rsync_down_args'),
                group=core.yarg.DEVELOPERS_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--remote-ya'],
                help='Path to \'ya\' on the remote machine',
                hook=core.yarg.SetValueHook('remote_ya_path'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--remote-env-ready'],
                help='Skip preparing remote environment',
                hook=core.yarg.SetConstValueHook('remote_prepare_env', False),
                group=core.yarg.DEVELOPERS_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--forward-key'],
                help='Literally, run ssh with -A argument when initiate remote dirs',
                hook=core.yarg.SetConstValueHook('forward_key', True),
                group=core.yarg.ADVANCED_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['--no-get-sources-build'],
                help='Don\'t download generated source files in build-only targets',
                hook=core.yarg.SetConstValueHook('in_build_get_source', False),
                group=core.yarg.ADVANCED_OPT_GROUP,
            )
        )

    def postprocess(self):
        if self.require_remote:
            if not self.remote_ya_path and self.remote_prepare_env:
                raise core.yarg.ArgsValidatingException(
                    'To prepare remote environment you must specify path to remote \'ya\' tool '
                    '(or use \'--remote-env-ready\')'
                )


def gen_ide_ymake_conf(params, toolchain_params, extra_flags, build_type=IDE_YMAKE_BUILD_TYPE):
    flags = copy.deepcopy(params.flags)
    flags.update(extra_flags)
    flags['IDE_MSVS_CALL'] = 'yes'
    if 'BUILD_LANGUAGES' not in flags:
        if getattr(params, 'java_fix', True):
            flags['BUILD_LANGUAGES'] = 'CPP PY2 PY3 JAVA'
        else:
            flags['BUILD_LANGUAGES'] = 'CPP PY2 PY3'
    flags['USE_CLANG'] = 'yes' if params.use_clang else 'no'

    return build.genconf.gen_conf(
        arc_dir=params.arc_root,
        conf_dir=os.path.join(params.bld_dir, 'confs'),
        build_type=build_type,
        use_local_conf=True,
        local_conf_path=None,
        extra_flags=flags,
        tool_chain=toolchain_params,
        conf_debug=params.conf_debug_options,
        debug_id='ide',
    )


class IdeProjectInfo(object):
    def __init__(
        self,
        params,
        app_ctx,
        title=None,
        output_path=None,
        instance_per_title=False,
        default_output_name='ide-project',
        default_output_here=False,
    ):
        self.params = params
        self.app_ctx = app_ctx
        self._instance_per_title = instance_per_title
        self.title = title if title else params.project_title
        self.output_path = output_path if output_path else params.project_output
        if not self.output_path:
            if default_output_here:
                self.output_path = os.getcwd()
            else:
                self.output_path = self.output_path_from_default(params, default_output_name)
        emit_message('Project title: [[imp]]{}[[rst]]'.format(self.title))
        emit_message('Project source path: [[path]]{}[[rst]]'.format(params.arc_root))
        emit_message('Project output path: [[path]]{}[[rst]]'.format(self.output_path))
        if instance_per_title:
            emit_message('Project instance: [[path]]{}[[rst]]'.format(self.instance_path))

    @staticmethod
    def output_path_from_default(params, default_output_name):
        return exts.path2.abspath(os.path.join(params.arc_root, '..', default_output_name))

    @property
    def instance_path(self):
        if self._instance_per_title:
            return os.path.join(self.output_path, self.title)
        return self.output_path


# Project storage persists between 'ya ide' launches
# Info is serialized into project output directory
class IdeProjectStorage(object):
    def __init__(self, project_info, subpath='.ya.ide.project.json'):
        self.project_info = project_info
        self.path = os.path.join(project_info.output_path, subpath)
        logger.debug('Project storage path: %s', self.path)
        self.data = self._load(self.path).setdefault(project_info.title, {})
        logger.debug('Loaded project stored data: %s', json.dumps(self.data, sort_keys=True, indent=2))

    def save(self):
        with exts.filelock.FileLock(self.path + '.lock'):
            data = self._load(self.path)
            data[self.project_info.title] = self.data
            data['_last'] = self.project_info.title
            self._save(self.path, data)
            logger.debug('Saved project stored data: %s', json.dumps(self.data, sort_keys=True, indent=2))

    @staticmethod
    def _load(path):
        if not os.path.isfile(path):
            logger.debug('No project storage found')
            return {}
        with open(path, 'rb') as st_file:
            data = json.load(st_file)
        return data

    @staticmethod
    def _save(path, data):
        exts.fs.create_dirs(os.path.dirname(path))
        with open(path, 'w') as st_file:
            json.dump(data, st_file, sort_keys=True, indent=2)


def memo_gen(storage):
    storage.data['gen'] = {
        'cmd': sys.argv,
        'cwd': os.getcwd(),
    }


def fix_win_path(path):
    return path.replace('\\', '/')


class IdeGraph(object):
    def __init__(self, params):
        self.params = params
        logger.debug('Generating graph')
        ya_make_extra = getattr(params, "ya_make_extra", [])
        opts = core.yarg.merge_opts(build.build_opts.ya_make_options(free_build_targets=True))
        build_params = opts.initialize(ya_make_extra)
        build_params.flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'yes'
        self.graph = build.gen_plan2.ya_make_graph(params, app, extra_ya_make_opts=build_params)
        logger.debug('Mining project data')
        self._mine()
        logger.debug('Files total: %s', len(self.files))
        logger.debug('Include directories: %s', self.inc_dirs)
        logger.debug('Defines: %s', self.defines)

    def _mine(self):
        def is_compile_node(arg_node):
            kv = arg_node.get('kv', {})
            p = kv.get('p', '')
            if p != 'CC':
                return False
            src_ends = ('.cpp', '.c', '.cc', '.C', '.cxx')
            srcs = [src for src in arg_node['inputs'] if src.endswith(src_ends)]
            if len(srcs) == 1:
                return True
            return False

        self.files = set()
        self.inc_dirs = set()
        self.defines = set()

        def mine_opts(args):
            def add_item(collection, item, args):
                if len(item) > 2:
                    item = item[2:]
                else:
                    item = args.popleft()
                collection.add(item)

            def parse_current(current, args):
                if current.startswith(('-I', '/I')):
                    add_item(self.inc_dirs, current, args)
                elif current.startswith(('-D', '/D')):
                    add_item(self.defines, current, args)

            while args:
                current = args.popleft()
                parse_current(current, args)

        for node in self.graph.get('graph'):
            for x in node['inputs']:
                self.files.add(x)
            if is_compile_node(node):
                for cmd in node.get('cmds', [node]):
                    mine_opts(collections.deque(cmd['cmd_args']))

    # XXX: rough implementation: using only single YMake configuration
    # XXX: maybe better use graph to store this info
    def mine_files(self, types=frozenset(('File',))):
        if not types:
            return
        filelist = build.build_facade.gen_filelist(
            build_root=None,
            build_type=IDE_YMAKE_BUILD_TYPE,
            build_targets=self.params.abs_targets,
            debug_options=[],
            flags={},
        )
        entry_re = re.compile('^file: (?P<entry_type>[a-zA-Z0-9_]+) (?P<path>.+)$')
        orig_count = len(self.files)
        for line in six.StringIO(filelist.stdout):
            matched_entry = entry_re.match(line)
            if not matched_entry:
                continue
            if matched_entry.group('entry_type') not in types:
                continue
            path = matched_entry.group('path')
            if not path.startswith('$S/'):
                logger.debug('Weird source file mined: %s', path)
                continue
            self.files.add(path.replace('$S', '$(SOURCE_ROOT)'))
        logger.debug('Mined %s additional source files', len(self.files) - orig_count)

    def iter_source_files(self):
        return (f for f in self.files if build.graph_path.GraphPath(f).source)

    def iter_build_files(self):
        return (f for f in self.files if build.graph_path.GraphPath(f).build)

    def get_modules(self, roots=None, strip_non_final_targets=False):
        all_module_names = collections.Counter()

        def uniq_module_name(m_name):
            qty = all_module_names[m_name]
            all_module_names[m_name] += 1
            suff = '_{}'.format(qty) if qty else ''
            return m_name + suff

        modules = {}
        results = frozenset(self.graph.get('result'))
        for node in self.graph.get('graph'):
            if node['uid'] not in results:
                continue
            target_properties = node.get('target_properties', {})
            module_type = target_properties.get('module_type', "")
            if strip_non_final_targets and module_type != 'bin' and module_type != 'so':
                continue
            main_output = node['outputs'][0].replace('$(BUILD_ROOT)/', '')
            module_path = os.path.dirname(main_output)
            module_name = uniq_module_name(os.path.basename(main_output))
            if roots is None or any(module_path.startswith(x) for x in roots):
                modules[module_name] = {
                    'path': main_output,
                    'module_path': module_path,
                    'runnable': target_properties.get('module_type') == 'bin',
                }
        return modules

    def sync_targets(self, source_root, add_extra=True, exclude_regexps=None):
        source_root = source_root.rstrip('/')
        sync_files = {
            build.graph_path.GraphPath(fl).resolve(source_root=source_root) for fl in self.iter_source_files()
        }

        def get_dir_content(path):
            return list(filter(os.path.isfile, [os.path.join(path, x) for x in os.listdir(path)]))

        def get_subdirs(path):
            return list(filter(os.path.isdir, [os.path.join(path, x) for x in os.listdir(path)]))

        def get_whole_dir(path):
            return (
                os.path.join(w_root, w_file)
                for w_root, _, w_files in exts.os2.fastwalk(path, followlinks=True)
                # filter broken symlinks
                for w_file in w_files
                if os.path.exists(os.path.join(w_root, w_file))
            )

        def get_parent_cmakelists(path):
            path = os.path.dirname(path).rstrip(os.path.sep)
            while not source_root.startswith(path):
                for cmake_path in (os.path.join(path, x) for x in build.makelist.MAKELIST_FILENAMES):
                    if os.path.exists(cmake_path):
                        yield cmake_path
                path = os.path.normpath(os.path.join(path, '..')).rstrip(os.path.sep)

        if add_extra:
            dirs = {os.path.dirname(fl) for fl in sync_files}
            sync_contrib = False

            for directory in dirs:
                dir_content = get_dir_content(directory)
                sub_dirs = get_subdirs(directory)
                ut_subdir = os.path.join(directory, 'ut')
                if os.path.exists(ut_subdir):
                    sync_files.update(get_dir_content(ut_subdir))
                    if ut_subdir in sub_dirs:
                        sub_dirs.remove(ut_subdir)
                if 'contrib' in exts.path2.path_explode(directory):
                    sync_contrib = True
                else:
                    sync_files.update(dir_content)
                sync_files.update(cmake_file for cmake_file in get_parent_cmakelists(directory))
            contrib_dir = os.path.join(source_root, 'contrib')
            if sync_contrib and os.path.isdir(contrib_dir):
                sync_files.update(get_whole_dir(contrib_dir))

        extra_dev_dirs = (os.path.join('devtools', 'ya'), 'build')
        sync_files.update(
            itertools.chain.from_iterable(get_whole_dir(os.path.join(source_root, path)) for path in extra_dev_dirs)
        )
        sync_files.add(os.path.join(source_root, build.makelist.MAKELIST_FILENAME_PREFERRED))

        if exclude_regexps:
            for pattern in exclude_regexps:
                to_exclude = []
                for fl in sync_files:
                    match = re.match(pattern, fl)
                    if match and match.end() == len(fl):
                        to_exclude.append(fl)
                sync_files.difference_update(to_exclude)

        sync_files = [os.path.relpath(x, source_root) for x in sync_files]
        return sync_files


class CMakeStubGenerationException(Exception):
    mute = True


class CMakeStubProject(object):
    YA_MAKE_CMD = 'add_custom_target({name} COMMAND {wrapper}${{PROJECT_SOURCE_DIR}}/ya make --build=${{CMAKE_BUILD_TYPE}} --output=${{PROJECT_OUTPUT_DIR}}{opts} {targets})'
    SOURCE_EXTS = ('h', 'cpp', 'cc', 'c', 'cxx', 'C')
    FORBIDDEN_TARGET_NAMES = ('all', 'help', 'test')
    IDENTIFY_COMMENT = '# __YA_IDE_CMAKE_STUB_FILE__'

    def __init__(
        self,
        params,
        app_ctx,
        info,
        filename='CMakeLists.txt',
        required_cmake_version=None,
        need_source_symlinks=False,
        mask_output_roots=False,
        header=None,
    ):
        self.params = params
        self.app_ctx = app_ctx
        self.info = info
        self.project_path = os.path.join(info.instance_path, filename)
        self._stub_path = self.project_path
        self.header = header or self.IDENTIFY_COMMENT
        self.required_cmake_version = required_cmake_version
        self.source_symlinks = need_source_symlinks or params.ya_make_symlinks
        self.mask_output_roots = mask_output_roots
        self.project_files = set()
        self.targets = set()
        self.inc_dirs = set()
        emit_message('Project path: [[path]]{}[[rst]]'.format(self.project_path))
        logger.debug('Required CMake version: %s', self.required_cmake_version)
        logger.debug('Extra make args: %s', self.params.ya_make_extra)
        logger.debug('Make symlinks: %s', self.source_symlinks)

    def generate(
        self,
        joint_target=False,
        codegen_target=False,
        filters=None,
        custom_block_base_vars='',
        custom_block='',
        src_dir=None,
        out_dir=None,
        remote_dir=None,
        wrapper=None,
        makelists=False,
        generated=False,
        all_source_files=False,
        forbid_cmake_override=False,
        use_sync_server=False,
        strip_non_final=False,
    ):
        if forbid_cmake_override and os.path.exists(self._stub_path):
            with open(self._stub_path) as fl:
                first_line = fl.readline().strip()
            if first_line != self.header:
                emit_message(
                    '[[bad]]File {} already exists. '
                    'If it\'s automatically generated delete it and rerun generation (needed one time).[[rst]]'.format(
                        exts.path2.abspath(self._stub_path)
                    )
                )
                raise CMakeStubGenerationException()
        self.ide_graph = IdeGraph(self.params)
        additional_mining = set()
        if makelists:
            additional_mining.add('MakeFile')
        if all_source_files:
            additional_mining.add('File')
        self.ide_graph.mine_files(types=additional_mining)
        custom_block += """\


message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build configuration: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "Output directory: ${PROJECT_OUTPUT_DIR}")
message(STATUS "CMake directory: ${PROJECT_BINARY_DIR}")
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    message(FATAL_ERROR "Build configuration is not set")
endif()
"""
        files = self.ide_graph.iter_source_files()
        if generated:
            files = itertools.chain(
                files,
                (
                    f
                    for f in self.ide_graph.iter_build_files()
                    if any(f.endswith('.' + ext) for ext in self.SOURCE_EXTS)
                ),
            )
        if not src_dir:
            src_dir = self.params.arc_root
        if not out_dir:
            out_dir = '${PROJECT_BINARY_DIR}'
        self.project_files, self.targets = self._create_stub(
            src_dir,
            out_dir,
            remote_dir,
            files,
            joint_target,
            codegen_target,
            filters,
            custom_block_base_vars,
            custom_block,
            wrapper,
            use_sync_server,
            strip_non_final,
        )
        self.inc_dirs = self.ide_graph.inc_dirs
        logger.debug('Targets: %s', ', '.join(sorted(six.iterkeys(self.targets))))
        logger.debug('Project files: %s', len(self.project_files))
        logger.debug('Project incl dirs: %s', len(self.inc_dirs))
        self.binaries = {k: v.get('path') for k, v in six.iteritems(self.targets) if v.get('runnable')}
        logger.debug('Binaries: %s', ', '.join(six.iterkeys(self.binaries)))

    def _create_stub(
        self,
        src_dir,
        out_dir,
        remote_dir,
        files,
        joint_target,
        codegen_target,
        filters,
        custom_block_base_vars,
        custom_block,
        wrapper,
        use_sync_server,
        strip_non_final,
    ):
        def subst(s):
            res = build.graph_path.resolve_graph_value(
                s, source_root='${PROJECT_SOURCE_DIR}', build_root='${PROJECT_OUTPUT_DIR}'
            )
            return RE_ESCAPE_IN_UNQUOTED.sub(r'\\\g<0>', res)

        def gen_ya_make_cmd(name, paths, with_tests, replace_result=False, extra=None):
            target_name = name + '_' if name in self.FORBIDDEN_TARGET_NAMES else name
            make_opts = ['--add-result=.{}'.format(ext) for ext in self.SOURCE_EXTS]
            if remote_dir:
                make_opts = ['$<$<NOT:$<BOOL:${{YA_REMOTE}}>>:{}>'.format(opt) for opt in make_opts]
            if with_tests:
                make_opts.append('-t')
            if not self.source_symlinks:
                make_opts.append('--no-src-links')
            if self.mask_output_roots:
                make_opts.append('--mask-roots')
            else:
                make_opts.append('--no-mask-roots')
            make_opts.append('$<$<BOOL:${YA_SANITIZE}>:--sanitize>')
            make_opts.append('$<$<BOOL:${YA_SANITIZE}>:${YA_SANITIZE}>')
            if replace_result:
                make_opts.append('--replace-result')
            if extra:
                make_opts.extend(extra)
            make_opts.extend(['-T', '--no-emit-status'])
            make_opts_str = (' ' + ' '.join(opt.replace(' ', r'\ ') for opt in make_opts)) if make_opts else ''
            targets = ['${{PROJECT_SOURCE_DIR}}/{}'.format(fix_win_path(path)) for path in paths]
            build_command = self.YA_MAKE_CMD.format(
                name=target_name,
                wrapper=(wrapper + ' ') if wrapper else '',
                opts=make_opts_str,
                targets=' '.join(targets),
            )
            if hasattr(self.params, 'remote_host') and self.params.remote_host:
                module_name = target_name if target_name != JOINT_TARGET_NAME else JOINT_TARGET_REMOTE_NAME
                # backslash is necessary as rsync treats 'source_path' and 'source_path/' in different ways
                remote_output_subdir = (
                    os.path.join(self.params.remote_cache_path, REMOTE_OUTPUT_SUBDIR).rstrip(os.path.sep) + os.path.sep
                )
                remote_source_subdir = os.path.join(self.params.remote_cache_path, REMOTE_SOURCE_SUBDIR)
                remote_build_dir = os.path.join(self.params.remote_cache_path, REMOTE_BUILD_SUBDIR)
                remote_build_command = (
                    'add_custom_target(REMOTE_BUILD_{module_name}\n'
                    'COMMAND ${{PROJECT_SOURCE_DIR}}/ya tool rsync {rsync_up_args} '
                    '--files-from={sync_file} ${{PROJECT_SOURCE_DIR}} '
                    '{host}:{remote_source_path}\n'
                    + (
                        'COMMAND ssh {host} \'find {remote_output} -mindepth 1 -delete 2>/dev/null || echo\'\n'
                        if self.params.remote_clean_output
                        else ''
                    )
                    + 'COMMAND ${{PROJECT_SOURCE_DIR}}/ya tool python -- '
                    '${{PROJECT_SOURCE_DIR}}/devtools/qt/unmask_roots.py '
                    '${{PROJECT_SOURCE_DIR}} ${{PROJECT_BINARY_DIR}} '
                    'ssh {host} {remote_source_path}/ya make {remote_target} '
                    '--build=${{CMAKE_BUILD_TYPE}} '
                    '--output={remote_output} --build-dir={remote_build_dir} {remote_build_options}'
                    + (
                        '\nCOMMAND ssh {host} \'cd {remote_output} && find -type f -not -perm /111\' |'
                        ' ${{PROJECT_SOURCE_DIR}}/ya tool rsync --files-from=- {rsync_down_args} '
                        '{host}:{remote_output} ${{PROJECT_OUTPUT_DIR}}'
                        if self.params.in_build_get_source
                        else ''
                    )
                    + ')'
                )
                remote_download_command = (
                    'add_custom_target(REMOTE_BUILD_AND_DOWNLOAD_{module_name}\n'
                    'COMMAND ${{PROJECT_SOURCE_DIR}}/ya tool rsync {rsync_down_args} '
                    '{host}:{remote_output} ${{PROJECT_OUTPUT_DIR}}'
                    ')'
                )
                dependency = 'add_dependencies(REMOTE_BUILD_AND_DOWNLOAD_{module_name} REMOTE_BUILD_{module_name})'
                remote_args = dict(
                    module_name=module_name,
                    remote_target=' '.join(os.path.join(remote_source_subdir, target_path) for target_path in paths),
                    remote_source_path=remote_source_subdir,
                    host=self.params.remote_host,
                    ya_dir_name='ya',
                    sync_file=os.path.join(self.info.instance_path, SYNC_LIST_FILE_NAME),
                    remote_output=remote_output_subdir,
                    remote_build_options=make_opts_str,
                    rsync_up_args=self.params.rsync_upload_args,
                    rsync_down_args=self.params.rsync_down_args,
                    remote_build_dir=remote_build_dir,
                )
                build_command = '\n'.join(
                    [build_command]
                    + [x.format(**remote_args) for x in [remote_build_command, remote_download_command, dependency]]
                )

            return build_command

        min_version = self.required_cmake_version if self.required_cmake_version else '2.8'

        roots = filters or ['']
        exts.fs.create_dirs(os.path.dirname(self._stub_path))
        stub_tmp_path = self._stub_path + '.tmp'
        with open(stub_tmp_path, 'w') as stub:

            def emit(s=''):
                stub.write(s)
                stub.write('\n')

            def emit_block(block):
                if not block:
                    return
                if isinstance(block, (str, six.text_type)):
                    block = block.split('\n')
                for line in block:
                    emit(line)

            emit(self.header)

            emit('cmake_minimum_required(VERSION {})'.format(min_version))
            emit('project({})'.format(self.info.title))
            emit_block(custom_block_base_vars)
            if remote_dir:
                emit('if (YA_REMOTE)')
                emit('    set(PROJECT_SOURCE_DIR {})'.format(fix_win_path(remote_dir)))
                emit('else()')
                emit('    set(PROJECT_SOURCE_DIR {})'.format(fix_win_path(src_dir)))
                emit('endif()')
            else:
                emit('set(PROJECT_SOURCE_DIR {})'.format(fix_win_path(src_dir)))
            emit('set(PROJECT_OUTPUT_DIR {})'.format(out_dir))
            emit('set(CMAKE_CXX_STANDARD 20)')
            emit_block(custom_block)

            filtered_files = set(
                (f for f in files if any(build.graph_path.GraphPath(f).strip().startswith(x) for x in roots))
            )

            indent = ''
            if remote_dir:
                emit('if (NOT YA_REMOTE)')
                indent = '    '
            emit(indent + 'set(SOURCE_FILES')
            for x in sorted(filtered_files):
                emit(indent + '    ' + subst(x))
            emit(indent + ')')
            emit(indent + 'add_library(_arcadia_fake_do_not_build ${SOURCE_FILES})')
            emit(indent + 'set_target_properties(_arcadia_fake_do_not_build PROPERTIES LINKER_LANGUAGE CXX)')
            if remote_dir:
                emit('endif()')
            emit()

            for x in sorted(self.ide_graph.inc_dirs):
                emit('include_directories({})'.format(subst(x)))
            emit()
            cmake_defines = [x for x in self.ide_graph.defines.copy() if not x.startswith('FAKEID')]
            emit('add_definitions({})'.format(' '.join(['-D' + subst(x) for x in cmake_defines])))
            emit()

            targets = self.ide_graph.get_modules(roots, strip_non_final)
            for module_name in sorted(six.iterkeys(targets)):
                emit(
                    gen_ya_make_cmd(
                        module_name, [targets[module_name]['module_path']], False, False, self.params.ya_make_extra
                    )
                )
            if joint_target:
                emit(
                    gen_ya_make_cmd(JOINT_TARGET_NAME, self.params.rel_targets, False, False, self.params.ya_make_extra)
                )
            if codegen_target:
                emit(
                    gen_ya_make_cmd(
                        CODEGEN_TARGET_NAME, self.params.rel_targets, False, True, self.params.ya_make_extra
                    )
                )

            if use_sync_server and remote_dir:
                emit()
                emit('if (YA_REMOTE)')
                emit('    add_custom_target(')
                emit('        _arcadia_remote_sync')
                emit('        COMMAND echo "Sync local changes..."')
                emit(
                    '        COMMAND curl --silent --show-error --fail --unix-socket ${CMAKE_CURRENT_SOURCE_DIR}/pull.socket http://socket/pull'
                )
                emit('        COMMAND echo "Done")')

                for module_name in sorted(six.iterkeys(targets)):
                    emit(
                        '    add_dependencies({} _arcadia_remote_sync)'.format(
                            module_name + '_' if module_name in self.FORBIDDEN_TARGET_NAMES else module_name
                        )
                    )
                if joint_target:
                    emit('    add_dependencies({} _arcadia_remote_sync)'.format(JOINT_TARGET_NAME))
                if codegen_target:
                    emit('    add_dependencies({} _arcadia_remote_sync)'.format(CODEGEN_TARGET_NAME))
                emit('endif()')

        exts.fs.replace_file(stub_tmp_path, self._stub_path)
        return filtered_files, targets


class RemoteIdeError(Exception):
    mute = True


def set_up_remote(params, app_ctx):
    sshp = SSHProxy(params.remote_host, ssh_args=['-A'] if params.forward_key else None)

    if not sshp.is_host_up():
        raise RemoteIdeError('Host is not reachable.')

    if params.remote_prepare_env:
        emit_message('Preparing remote environment')
        if not params.remote_ya_path:
            raise RemoteIdeError('Path to remote Ya tool isn\'t specified.')
        if not sshp.is_exec(params.remote_ya_path):
            raise RemoteIdeError('Can\'t find remote ya binary')
        source_dir = os.path.join(params.remote_cache_path, REMOTE_SOURCE_SUBDIR)
        sshp.mkdir_p(params.remote_cache_path)
        if not sshp.is_dir(source_dir):
            try:
                sshp.ya_clone(params.remote_ya_path, source_dir)
                if 'darwin' == yalibrary.platform_matcher.my_platform():
                    sshp.checkout(source_dir, [DUMMY_CPP_PROJECT_ARCADIA_PATH])
            except subprocess.CalledProcessError:
                if 'SvnRuntimeError' in traceback.format_exc():
                    emit_message(
                        '[[warn]]Can\'t use svn to clone Arcadia on remote host. '
                        'Could it be that you forgot [[imp]]--forward-key[[warn]] option?[[rst]]'
                    )
                raise
        emit_message('Remote environment set')


class RemoteDevEnvOptions(core.yarg.Options):
    def __init__(self, **kwargs):
        self.source_mine_hacks = True

    @staticmethod
    def consumer():
        return core.yarg.ArgConsumer(
            ['--simple-source-mining'],
            help='Turn off special mining for source files (works if host is specified)',
            hook=core.yarg.SetConstValueHook('source_mine_hacks', False),
            group=core.yarg.DEVELOPERS_OPT_GROUP,
        )


def get_pidfile_path(cache_path, port):
    return os.path.join(cache_path, 'server', 'gdbserver-{0}.pid'.format(port))


def emit_message(*args):
    try:
        import app_ctx
    except ImportError:
        # Tests doesn't contain app_ctx
        app_ctx = FakeAppCtx()
    return app_ctx.display.emit_message(*args)


def setup_tidy_config(source_root):
    config_path = os.path.join(source_root, "build/config/tests/clang_tidy/config.yaml")
    if not os.path.exists(config_path):
        emit_message("[[warn]]Failed to find clang_tidy's config[[rst]]: '{}' doesn't exist".format(config_path))
        return

    target_name = ".clang-tidy"
    target_path = os.path.join(source_root, target_name)

    create_symlink = False
    if not os.path.lexists(target_path):
        create_symlink = True
    elif os.path.islink(target_path):
        link_path = os.readlink(target_path)
        if link_path != config_path:
            emit_message(
                "[[warn]]Clang_tidy's config was updated[[rst]]: '{}' was linked to the '{}', new path: '{}'".format(
                    target_path,
                    link_path,
                    config_path,
                ),
            )
            os.unlink(target_path)
            create_symlink = True
    else:
        emit_message(
            "[[warn]]Failed to create link to the clang_tidy's config[[rst]]: '{}' is not a link".format(target_path)
        )

    if create_symlink:
        os.symlink(config_path, target_path)
