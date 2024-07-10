from __future__ import absolute_import
import glob
import exts.yjson as json
import logging
import os
import re
import platform
import itertools

import six

import exts.fs
import exts.path2
import exts.process
import exts.strings
import exts.tmp
import exts.windows

import yalibrary.qxml

import core.yarg

import ide.ide_common

logger = logging.getLogger(__name__)

DEFAULT_QT_OUTPUT_DIR = 'qt'
DEFAULT_QT_VERSION = '3.6'
VALID_QT_PROJECT_TYPES = ('cmake',)
DEFAULT_QT_PROJECT_TYPE = 'cmake'
QT_REMOTE_SETTINGS_DIR = 'qtconfig'

RE_VERSION = re.compile(r'^(?P<major>[0-9]+)(?:\.(?P<minor>[0-9]+)(?:\.(?P<patch>[0-9]+))?)?$')
RE_TOOL_NAME = re.compile(r'^ya_ide_qt_(?P<name>[a-zA-Z0-9_]+)\.xml$')


class IdeQtError(Exception):
    mute = True


class IdeQtInvalidConfError(IdeQtError):
    pass


class IdeQtDiscoverConfError(IdeQtError):
    pass


class QtConfigOptions(core.yarg.Options):
    def __init__(self, **kwargs):
        self.kit = None

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--kit'],
                help='Specify QtCreator kit to use',
                hook=core.yarg.SetValueHook('kit'),
                group=core.yarg.DEVELOPERS_OPT_GROUP,
            ),
        ]


class QtDevEnvOptions(core.yarg.Options):
    def __init__(self, **kwargs):
        self.reset = True
        self.verify = True

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--no-reset'],
                help='Avoid removing .user project settings or build cache',
                hook=core.yarg.SetConstValueHook('reset', False),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--no-verify'],
                help='Skip QtCreator config verification',
                hook=core.yarg.SetConstValueHook('verify', False),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class GenQtOptions(core.yarg.Options):
    def __init__(self, **kwargs):
        self.project_type = DEFAULT_QT_PROJECT_TYPE
        self.install = False
        self.symlink = True

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['--install'],
                help='Install required configs for QtCreator (modifies user configuration)',
                hook=core.yarg.SetConstValueHook('install', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--project-type'],
                help='Project type: {}'.format(', '.join(str(x) for x in VALID_QT_PROJECT_TYPES)),
                hook=core.yarg.SetValueHook('project_type'),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--no-symlink'],
                help='Do not create symlink to Arcadia in project directory. Always set for Arc VCS.',
                hook=core.yarg.SetConstValueHook('symlink', False),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class QtCreatorOptions(core.yarg.Options):
    def __init__(self):
        self.ide_path = None
        self.run = False
        self.daemonize = False

    @staticmethod
    def consumer():
        return (
            core.yarg.ArgConsumer(
                ['-I', '--ide-path'],
                help='Path to QtCreator binary (normally it will be autodetected)',
                hook=core.yarg.SetValueHook('ide_path'),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            )
            + core.yarg.ArgConsumer(
                ['-R', '--run'],
                help='Run Qt Creator on the created project. '
                'If it\'s a remote project, it will be run with environment ready for remote debugging',
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
                hook=core.yarg.SetConstValueHook('run', True),
            )
            + core.yarg.ArgConsumer(
                ['--daemonize'],
                help='Makes --run to start QtCreator like a daemon',
                hook=core.yarg.SetConstValueHook('daemonize', True),
            )
        )

    def postprocess(self):
        if self.ide_path:
            self.ide_path = exts.path2.abspath(self.ide_path, expand_user=True)
            if not os.path.isfile(self.ide_path) and not os.access(self.ide_path, os.X_OK):
                raise core.yarg.ArgsValidatingException(
                    '{} doesn\'t look like an executable file'.format(self.ide_path)
                )
        if self.daemonize and not self.run:
            raise core.yarg.ArgsValidatingException('--daemon option requires --run one')


QT_OPTS = ide.ide_common.ide_via_ya_make_opts() + [
    ide.ide_common.IdeProjectVersionOptions(),
    QtDevEnvOptions(),
    QtConfigOptions(),
    GenQtOptions(),
    ide.ide_common.RemoteOptions(),
    ide.ide_common.IdeRemoteOptions(),
    ide.ide_common.RemoteDevEnvOptions(),
    QtCreatorOptions(),
]


def find_qt_creator():
    def candidates():
        def iter_patterns(roots, subpaths):
            for root, subpath in itertools.product(roots, subpaths):
                for path in glob.iglob(os.path.join(root, subpath)):
                    yield path

        if platform.system() == 'Linux':
            for path in iter_patterns(
                (
                    os.path.expanduser('~'),
                    '/opt',
                ),
                (
                    'qtcreator*/bin/qtcreator',
                    'Qt*/Tools/QtCreator/bin/qtcreator',
                ),
            ):
                yield path
            yield '/usr/bin/qtcreator'
        elif platform.system() == 'Darwin':
            for path in iter_patterns(
                (
                    os.path.expanduser('~/Applications'),
                    os.path.expanduser('~'),
                    '/Applications',
                ),
                (
                    'Qt Creator.app/Contents/MacOS/Qt Creator',
                    'Qt*/Qt Creator.app/Contents/MacOS/Qt Creator',
                ),
            ):
                yield path

    execs = [cand for cand in candidates() if os.path.isfile(cand) and os.access(cand, os.X_OK)]
    logger.debug('Candidates for QtCreator are: %s', execs)
    return execs[0] if execs else None


def discover_qt_config():
    if exts.windows.on_win():
        raise NotImplementedError('QtCreator config discovery is not supported on Windows for now')
    config_path = exts.path2.abspath(os.path.join('~', '.config', 'QtProject'), expand_user=True)
    if not os.path.isdir(config_path):
        raise IdeQtDiscoverConfError('Cannot discover QtCreator config directory: {}'.format(config_path))
    logger.debug('Discovered config: %s', config_path)
    return config_path


def qt_version(version):
    matched = RE_VERSION.match(version)
    if not matched:
        raise IdeQtError('Cannot parse QtCreator version: {}'.format(version))
    if not matched.group('minor'):
        return int(matched.group('major')), 0
    return int(matched.group('major')), int(matched.group('minor'))


class QtConfigFile(object):
    VERSION = 1
    ELEM = 'Elem'
    ELEM_ID_KEY = 'Elem.Id'
    DOCTYPE = 'QtCreatorElems'
    DEFAULT_FIELD = False

    def __init__(self, path):
        self.path = path
        if not os.path.isfile(self.path):
            raise IdeQtError('QtCreator config not found: {}'.format(self.path))
        self.data, self.root_tag = yalibrary.qxml.load(self.path)
        if not self.validate():
            raise IdeQtError('Invalid QtCreator config: {}'.format(self.path))

    @classmethod
    def validate_data(cls, data):
        assert isinstance(data, dict)
        assert isinstance(data.get('Version'), int)
        assert data['Version'] == cls.VERSION
        assert isinstance(data.get(cls.ELEM + '.Count'), int)
        assert data[cls.ELEM + '.Count'] >= 0
        assert not cls.DEFAULT_FIELD or isinstance(data.get(cls.ELEM + '.Default'), six.text_type)
        ids = set()
        for n in six.moves.xrange(data[cls.ELEM + '.Count']):
            key = '{}.{}'.format(cls.ELEM, n)
            assert isinstance(data.get(key), dict)
            assert isinstance(data[key].get(cls.ELEM_ID_KEY), six.text_type)
            ids.add(data[key][cls.ELEM_ID_KEY])
        assert not cls.DEFAULT_FIELD or not ids or not data[cls.ELEM + '.Default'] or data[cls.ELEM + '.Default'] in ids

    def validate(self):
        try:
            assert self.root_tag == 'qtcreator'
            self.validate_data(self.data)
        except AssertionError:
            return False
        return True

    def save(self):
        assert self.validate()
        logger.debug('Updating config file: %s', self.path)
        yalibrary.qxml.save(self.path, self.data, self.root_tag, doctype=self.DOCTYPE)

    def _elem_keys(self):
        return {
            elem[self.ELEM_ID_KEY]: k
            for k, elem in six.iteritems(self.data)
            if k.startswith(self.ELEM + '.') and isinstance(elem, dict) and self.ELEM_ID_KEY in elem
        }

    def ids(self):
        return frozenset(six.iterkeys(self._elem_keys()))

    def has_id(self, elem_id):
        return elem_id in self.ids()

    def get(self, elem_id):
        return self.data.get(self._elem_keys().get(elem_id))

    def add(self, elem_id):
        if self.has_id(elem_id):
            logger.debug('Rewriting QtCreator %s: %s', self.ELEM, elem_id)
            elem = self.get(elem_id)
            elem.clear()
        else:
            elem = self.data.setdefault('{}.{}'.format(self.ELEM, self.data[self.ELEM + '.Count']), {})
            self.data[self.ELEM + '.Count'] += 1
            if self.DEFAULT_FIELD and self.data[self.ELEM + '.Count'] == 1:
                self.data[self.ELEM + '.Default'] = exts.strings.to_unicode(elem_id)
        elem[self.ELEM_ID_KEY] = exts.strings.to_unicode(elem_id)
        return elem


class QtProfileConfig(QtConfigFile):
    ELEM = 'Profile'
    ELEM_ID_KEY = 'PE.Profile.Id'
    DOCTYPE = 'QtCreatorProfiles'
    DEFAULT_FIELD = True

    def add_profile(self, profile_id, name, tools, cmake=True, gdb_inline=None):
        profile = self.add(profile_id)
        profile.update(
            {
                'PE.Profile.Name': exts.strings.to_unicode(name),
                'PE.Profile.Icon': u':///DESKTOP///',
                'PE.Profile.SDK': False,
                'PE.Profile.MutableInfo': [],
                'PE.Profile.AutoDetected': False,
            }
        )
        data = profile.setdefault('PE.Profile.Data', {})
        data.update(
            {
                'PE.Profile.Device': u'Desktop Device',
                'PE.Profile.DeviceType': 'Desktop',
                'PE.Profile.SysRoot': u'',
                'PE.Profile.ToolChain': exts.strings.to_unicode(tools['c++-clang']),
                'QtSupport.QtInformation': -1,
            }
        )
        if cmake:
            data.update(
                {
                    'CMakeProjectManager.CMakeKitInformation': exts.strings.to_unicode(tools['cmake']),
                    'CMake.ConfigurationKitInformation': [
                        u'CMAKE_CXX_COMPILER:STRING=%{Compiler:Executable}',
                    ],
                    'CMake.GeneratorKitInformation': u'CodeBlocks - Unix Makefiles',
                }
            )
        if gdb_inline is None:
            data['Debugger.Information'] = exts.strings.to_unicode(tools['gdb'])
        else:
            binary, engine_type = gdb_inline
            data['Debugger.Information'] = {
                'Binary': exts.strings.to_unicode(binary),
                'EngineType': engine_type,
            }
            exts.strings.to_unicode(tools['gdb'])
        self.save()


class QtToolChainConfig(QtConfigFile):
    ELEM = 'ToolChain'
    ELEM_ID_KEY = 'ProjectExplorer.ToolChain.Id'
    DOCTYPE = 'QtCreatorToolChains'

    def add_toolchain(self, toolchain_id, name, path, abi):
        self.add(toolchain_id).update(
            {
                'ProjectExplorer.ToolChain.DisplayName': exts.strings.to_unicode(name),
                'ProjectExplorer.ToolChain.Autodetect': False,
                'ProjectExplorer.GccToolChain.Path': exts.strings.to_unicode(path),
                'ProjectExplorer.GccToolChain.TargetAbi': exts.strings.to_unicode(abi),
            }
        )
        self.save()


class QtDebuggerConfig(QtConfigFile):
    ELEM = 'DebuggerItem'
    ELEM_ID_KEY = 'Id'
    DOCTYPE = 'QtCreatorDebugger'

    def add_debugger(self, debugger_id, name, path, abi, engine_type):
        self.add(debugger_id).update(
            {
                'DisplayName': exts.strings.to_unicode(name),
                'Binary': exts.strings.to_unicode(path),
                'AutoDetected': False,
                'EngineType': engine_type,
                'Abis': [
                    exts.strings.to_unicode(abi),
                ],
            }
        )
        self.save()


class QtCMakeToolsConfig(QtConfigFile):
    ELEM = 'CMakeTools'
    ELEM_ID_KEY = 'Id'
    DOCTYPE = 'QtCreatorCMakeTools'

    def add_cmaketools(self, cmaketools_id, name, path):
        self.add(cmaketools_id).update(
            {
                'DisplayName': exts.strings.to_unicode(name),
                'Binary': exts.strings.to_unicode(path),
                'AutoDetected': False,
            }
        )
        self.save()


class QtConfig(object):
    KIT_TOOLS = {
        'cmake': 'Ya.Ide.Qt.Kit.CMake',
        'c++-clang': 'ProjectExplorer.ToolChain.Clang:Ya.Ide.Qt.Kit.Cpp',
        'gdb': 'Ya.Ide.Qt.Kit.Gdb',
    }
    TOOLS = [
        'regen',
    ]
    GDB_ENGINE_TYPE = 1

    def __init__(self, params, app_ctx, path=None, gdb_path=None):
        self.params = params
        self.app_ctx = app_ctx
        self.path = path
        self.kit = params.kit if params.kit else 'Ya.Ide.Qt.Kit'
        self.kit_path = os.path.join(params.arc_root, 'devtools', 'qt', 'kit')
        self.tools_path = os.path.join(params.arc_root, 'devtools', 'qt', 'tools')
        self.platform = platform.system().lower()
        self.gdb_path = gdb_path or self.kitpath('gdb')
        self.cmaketools_loaded = False

    def subpath(self, s):
        return os.path.join(self.path, s)

    def qtcreator_subpath(self, s):
        return self.subpath(os.path.join('qtcreator', s))

    def kitpath(self, s):
        return os.path.join(self.kit_path, s)

    def toolspath(self, s):
        return os.path.join(self.tools_path, s)

    def _load(self):
        if self.cmaketools_loaded:
            return
        if not self.path:
            try:
                self.path = discover_qt_config()
            except IdeQtDiscoverConfError:
                self.app_ctx.display.emit_message(
                    '[[warn]]Cannot find QtCreator config files, you should install and launch QtCreator to generate its configuration[[rst]]'
                )
                raise
        if not os.path.isdir(self.path):
            raise IdeQtError('Cannot find QtCreator config directory: {}'.format(self.path))
        logger.debug('QtCreator config path: %s', self.path)
        logger.debug('Loading QtCreator config')
        self.profiles = QtProfileConfig(self.qtcreator_subpath('profiles.xml'))
        self.toolchains = QtToolChainConfig(self.qtcreator_subpath('toolchains.xml'))
        self.debuggers = None
        self.cmaketools = None
        if os.path.isfile(self.qtcreator_subpath('debuggers.xml')):
            # 3.0+
            self.debuggers = QtDebuggerConfig(self.qtcreator_subpath('debuggers.xml'))
        if os.path.isfile(self.qtcreator_subpath('cmaketools.xml')):
            # 3.5+
            self.cmaketools = QtCMakeToolsConfig(self.qtcreator_subpath('cmaketools.xml'))
        logger.debug('Searching for QtCreator external tools')
        self.tools = set()
        if os.path.isdir(self.qtcreator_subpath('externaltools')):
            for name in os.listdir(self.qtcreator_subpath('externaltools')):
                tool_path = os.path.join(self.qtcreator_subpath('externaltools'), name)
                matched_name = RE_TOOL_NAME.match(name)
                if not matched_name or not os.path.isfile(tool_path):
                    continue
                self.tools.add(matched_name.group('name'))
                logger.debug('Tool discovered: %s', name)
        logger.debug('QtCreator external tools: %s', ', '.join(sorted(self.tools)))
        self.cmaketools_loaded = True

    def verify(self):
        logger.debug('Verifying QtCreator config')
        try:
            self._load()
            if self.profiles and not self.profiles.has_id(self.kit):
                raise IdeQtInvalidConfError(
                    'Kit not found: {} (found: {})'.format(self.kit, ', '.join(sorted(self.profiles.ids())))
                )
            if self.toolchains and not self.toolchains.has_id(self.KIT_TOOLS['c++-clang']):
                raise IdeQtInvalidConfError(
                    'Toolchain not found: {} (found: {})'.format(
                        self.KIT_TOOLS['c++-clang'], ', '.join(sorted(self.toolchains.ids()))
                    )
                )
            if self.debuggers and not self.debuggers.has_id(self.KIT_TOOLS['gdb']):
                raise IdeQtInvalidConfError(
                    'Debugger not found: {} (found: {})'.format(
                        self.KIT_TOOLS['gdb'], ', '.join(sorted(self.debuggers.ids()))
                    )
                )
            if self.cmaketools and not self.cmaketools.has_id(self.KIT_TOOLS['cmake']):
                raise IdeQtInvalidConfError(
                    'CMake tools not found: {} (found: {})'.format(
                        self.KIT_TOOLS['cmake'], ', '.join(sorted(self.cmaketools.ids()))
                    )
                )
            not_installed_tools = set((tool for tool in self.TOOLS if tool not in self.tools))
            if not_installed_tools:
                self.app_ctx.display.emit_message(
                    '[[warn]]QtCreator external tools are available: {}, run [[imp]]ya ide qt --install[[warn]] to install them[[rst]]'.format(
                        ', '.join('[[imp]]{}[[warn]]'.format(x) for x in sorted(not_installed_tools))
                    )
                )
        except IdeQtDiscoverConfError:
            self.app_ctx.display.emit_message(
                '[[warn]]Also you should run [[imp]]ya ide qt --install[[warn]] before using generated project, '
                'or use [[imp]]--no-verify[[warn]] at your own risk[[rst]]'
            )
            raise
        except IdeQtInvalidConfError:
            self.app_ctx.display.emit_message(
                '[[warn]]QtCreator config verification failed, install required configuration with [[imp]]ya ide qt --install[[warn]], '
                'or use [[imp]]--no-verify[[warn]] at your own risk[[rst]]'
            )
            raise

    def install(self):
        self._load()
        assert not self.params.kit
        if not os.path.isdir(self.kit_path):
            raise IdeQtError('Ya Ide QtCreator Kit is not found in Arcadia: %s', self.kit_path)
        self.app_ctx.display.emit_message('Installing toolchain')
        self.toolchains.add_toolchain(
            self.KIT_TOOLS['c++-clang'], 'Ya Tool C++ (clang)', self.kitpath('c++'), self.abi()
        )
        gdb_inline = None
        if self.debuggers:
            self.app_ctx.display.emit_message('Installing debugger: [[path]]{}[[rst]]'.format(self.gdb_path))
            self.debuggers.add_debugger(
                self.KIT_TOOLS['gdb'], 'Ya Tool Gdb', self.gdb_path, self.abi(), self.GDB_ENGINE_TYPE
            )
        else:
            gdb_inline = (self.gdb_path, self.GDB_ENGINE_TYPE)
        if self.cmaketools:
            self.app_ctx.display.emit_message('Installing cmake')
            self.cmaketools.add_cmaketools(self.KIT_TOOLS['cmake'], 'Ya Tool CMake', self.kitpath('cmake'))
        self.app_ctx.display.emit_message('Installing profile')
        self.profiles.add_profile(
            self.kit, 'Ya', self.KIT_TOOLS, cmake=self.cmaketools is not None, gdb_inline=gdb_inline
        )
        exts.fs.ensure_dir(self.qtcreator_subpath('externaltools'))
        for tool in self.TOOLS:
            tool_src_path = self.toolspath('{}.xml'.format(tool))
            if not os.path.isfile(tool_src_path):
                raise IdeQtError('Cannot find external tool: {}'.format(tool_src_path))
            tool_dst_path = os.path.join(self.qtcreator_subpath('externaltools'), 'ya_ide_qt_{}.xml'.format(tool))
            self.app_ctx.display.emit_message('Installing external tool: [[imp]]{}[[rst]]'.format(tool))
            exts.fs.copy_file(tool_src_path, tool_dst_path)

    def abi(self):
        if self.platform == 'linux':
            return 'x86-linux-generic-elf-64bit'
        elif self.platform == 'darwin':
            return 'x86-macos-generic-mach_o-64bit'
        else:
            raise IdeQtError('Unsupported platform: {}'.format(self.platform))


class QtCMakeProject(object):
    def __init__(self, params, app_ctx, info, version, selected_targets=None):
        self.params = params
        self.app_ctx = app_ctx
        self.info = info
        self.version = version
        self.cmake_stub = ide.ide_common.CMakeStubProject(params, app_ctx, info, mask_output_roots=True)
        self.selected_targets = frozenset(
            (ide.ide_common.JOINT_TARGET_NAME,) if not selected_targets else selected_targets
        )
        logger.debug('Selected targets: %s', ', '.join(sorted(self.selected_targets)))

    def generate(self):
        custom_block_base_vars = ''
        if self.version < (4, 0):
            custom_block_base_vars += """\
if(DEFINED ENV{CMAKE_YA_IDE_QT_CONF})
    set(CMAKE_YA_IDE_QT_CONF "$ENV{CMAKE_YA_IDE_QT_CONF}")
    set(CMAKE_BUILD_TYPE "$ENV{CMAKE_YA_IDE_QT_CONF}")
endif()
"""
        exts.fs.ensure_dir(self.build_path)
        src_dir = os.path.join(self.build_path, 'source')
        if os.path.islink(src_dir):
            exts.fs.remove_file(src_dir)
        if self.use_symlink():
            exts.fs.symlink(self.params.arc_root, src_dir)
        else:
            src_dir = self.params.arc_root
        out_dir = self.output_conf_path('${CMAKE_YA_IDE_QT_CONF}')
        wrapper = ' '.join(
            (
                '${PROJECT_SOURCE_DIR}/ya',
                'tool',
                'python',
                '--',
                '${PROJECT_SOURCE_DIR}/devtools/qt/unmask_roots.py',
                '${PROJECT_SOURCE_DIR}',
                '${PROJECT_BINARY_DIR}',
            )
        )
        self.cmake_stub.generate(
            joint_target=True,
            src_dir=src_dir,
            out_dir=out_dir,
            custom_block_base_vars=custom_block_base_vars,
            wrapper=wrapper,
            makelists=True,
            all_source_files=True,
        )

    def build_conf_settings(self, conf):
        settings = {
            'id': u'CMakeProjectManager.CMakeBuildConfiguration',
            'env': [],
            'build_dir': exts.strings.to_unicode(self.build_conf_path(conf)),
            'build_steps': [
                {
                    'id': u'CMakeProjectManager.MakeStep',
                    'params': {
                        'CMakeProjectManager.MakeStep.Clean': False,
                        'CMakeProjectManager.MakeStep.UseNinja': False,
                        'CMakeProjectManager.MakeStep.BuildTargets': list(
                            exts.strings.to_unicode(x) for x in sorted(self.selected_targets)
                        ),
                    },
                },
            ],
            'build_steps_clean': [],
            'extra_conf': {
                'CMake.Configuration': [
                    u'CMAKE_BUILD_TYPE:STRING={}'.format(self._cmake_build_type(conf)),
                    u'CMAKE_YA_IDE_QT_CONF:STRING={}'.format(conf),
                    u'CMAKE_SKIP_PREPROCESSED_SOURCE_RULES=ON',
                    u'CMAKE_SKIP_ASSEMBLY_SOURCE_RULES=ON',
                    u'YACMAKE_SKIP_OBJECT_SOURCE_RULES=ON',
                ],
            },
        }

        if self.version < (4, 0):
            settings['env'].append(u'CMAKE_YA_IDE_QT_CONF={}'.format(self._cmake_build_type(conf)))

        if self.version >= (2, 8) and self.version < (3, 0):
            settings['extra_conf']['CMakeProjectManager.CMakeBuildConfiguration.BuildDirectory'] = (
                exts.strings.to_unicode(self.build_conf_path(conf))
            )

        return settings

    def run_conf_settings(self):
        settings = {
            'output_dir': exts.strings.to_unicode(self.output_conf_path('%{CurrentBuild:Name}')),
        }

        return settings

    def reset_build_cache(self, conf):
        if self.version >= (4, 0) or not os.path.isdir(self.build_conf_path(conf)):
            return
        for filename in os.listdir(self.build_conf_path(conf)):
            if not filename.endswith('.cbp'):
                continue
            path = os.path.join(self.build_conf_path(conf), filename)
            if not os.path.isfile(path):
                continue
            logger.debug('Removing CBP: %s', path)
            exts.fs.remove_file(path)
        cmake_cache_path = os.path.join(self.build_conf_path(conf), 'CMakeCache.txt')
        if os.path.isfile(cmake_cache_path):
            logger.debug('Removing CMake cache: %s', cmake_cache_path)
            exts.fs.remove_file(cmake_cache_path)

    @property
    def project_path(self):
        return self.cmake_stub.project_path

    @property
    def build_path(self):
        return self.info.instance_path

    def build_conf_path(self, conf):
        return os.path.join(self.build_path, conf)

    def output_conf_path(self, conf):
        return os.path.join(self.info.output_path, conf)

    @property
    def runnables(self):
        return self.cmake_stub.binaries

    def _cmake_build_type(self, conf):
        return conf

    def use_symlink(self):
        vcs_types, _, _ = yalibrary.vcs.detect()
        if not vcs_types:
            return self.params.symlink
        if 'arc' == vcs_types[0]:
            return False
        return self.params.symlink


def get_version_from_qtc(path_to_ide):
    if not path_to_ide:
        return None
    version_info = exts.process.run_process(path_to_ide, ['-version'], return_stderr=True, check=True)[1]
    version_string = re.search(r'Qt Creator ([0-9.]*)', version_info)
    return version_string.group(1) if version_string else None


class QtDevEnv(object):
    BUILD_CONFIGURATIONS = ('Debug', 'Release')

    def __init__(self, params, app_ctx, config, project_info, create_project, verify_config=True):
        self.params = params
        self.app_ctx = app_ctx
        self.config = config
        self.project_info = project_info
        path_to_ide = self.params.ide_path or find_qt_creator()
        if params.project_version or not path_to_ide:
            self.version = qt_version(params.project_version or DEFAULT_QT_VERSION)
        else:
            self.version = qt_version(get_version_from_qtc(path_to_ide) or DEFAULT_QT_VERSION)
        if not self._version_supported:
            raise IdeQtError('Unsupported QtCreator version: {}.{}'.format(*self.version))
        self.project_storage = ide.ide_common.IdeProjectStorage(self.project_info)
        self.project = create_project(params, app_ctx, self.project_info, self.version)
        ide.ide_common.memo_gen(self.project_storage)
        self.settings_path = self.project.project_path + '.shared'
        app_ctx.display.emit_message('QtCreator version: [[imp]]{}.{}[[rst]]'.format(*self.version))
        app_ctx.display.emit_message('Project settings: [[path]]{}[[rst]]'.format(self.settings_path))
        if verify_config and params.verify:
            config.verify()
        self.project_storage.data['ide_path'] = (
            exts.path2.abspath(path_to_ide, expand_user=True) if path_to_ide else None
        )
        self.project_storage.data['settings_path'] = None
        if path_to_ide:
            app_ctx.display.emit_message('Using QtCreator: {}'.format(path_to_ide))
        else:
            app_ctx.display.emit_message(
                '[[warn]]QtCreator is not detected and -I option is not used, you\'ll have to use -I option with \'run\' command further[[rst]]'
            )

    def generate(self):
        self.project.generate()
        self._gen_settings()
        if self.params.reset:
            self._reset()
            for conf in self.BUILD_CONFIGURATIONS:
                self.project.reset_build_cache(conf)
        self.project_storage.data['project_runnable'] = self.project_path
        self.project_storage.save()

    @property
    def project_path(self):
        return self.project.project_path

    @property
    def _settings_file_version(self):
        if self.version >= (4, 0):
            return 18
        if self.version >= (3, 0):
            return 15
        if self.version >= (2, 8):
            return 14
        assert False

    @property
    def _version_supported(self):
        return self.version >= (2, 8)

    def _gen_settings(self):
        settings = {
            'ProjectExplorer.Project.Updater.FileVersion': self._settings_file_version,
            'Version': self._settings_file_version,
            'ProjectExplorer.Project.TargetCount': 1,
            'ProjectExplorer.Project.ActiveTarget': 0,
            'ProjectExplorer.Project.Target.0': {
                'ProjectExplorer.ProjectConfiguration.Id': exts.strings.to_unicode(self.config.kit),
                'ProjectExplorer.Target.ActiveBuildConfiguration': 0,
                'ProjectExplorer.Target.ActiveRunConfiguration': 0,
            },
        }
        target = settings['ProjectExplorer.Project.Target.0']
        target.update(
            {
                'ProjectExplorer.Target.BuildConfigurationCount': len(self.BUILD_CONFIGURATIONS),
                'ProjectExplorer.Target.RunConfigurationCount': len(self.project.runnables),
            }
        )

        for num, conf in enumerate(self.BUILD_CONFIGURATIONS):
            build_configuration = target.setdefault('ProjectExplorer.Target.BuildConfiguration.{}'.format(num), {})
            build_configuration.update(
                {
                    'ProjectExplorer.ProjectConfiguration.DefaultDisplayName': exts.strings.to_unicode(conf),
                    'ProjectExplorer.ProjectConfiguration.DisplayName': exts.strings.to_unicode(conf),
                    'ProjectExplorer.BuildConfiguration.ClearSystemEnvironment': False,
                    'ProjectExplorer.BuildConfiguration.UserEnvironmentChanges': [],
                    'ProjectExplorer.BuildConfiguration.BuildStepListCount': 2,
                    'ProjectExplorer.BuildConfiguration.BuildStepList.0': {
                        'ProjectExplorer.ProjectConfiguration.Id': u'ProjectExplorer.BuildSteps.Build',
                    },
                    'ProjectExplorer.BuildConfiguration.BuildStepList.1': {
                        'ProjectExplorer.ProjectConfiguration.Id': u'ProjectExplorer.BuildSteps.Clean',
                    },
                }
            )

            project_build_conf = self.project.build_conf_settings(conf)
            logger.debug(
                'Project build conf settings (%s): %s', conf, json.dumps(project_build_conf, sort_keys=True, indent=2)
            )
            build_step_lists = ('build_steps', 'build_steps_clean')
            assert all(field in project_build_conf for field in ('id', 'build_dir') + build_step_lists)

            build_configuration['ProjectExplorer.ProjectConfiguration.Id'] = project_build_conf['id']
            build_configuration['ProjectExplorer.BuildConfiguration.UserEnvironmentChanges'].extend(
                project_build_conf.get('env', [])
            )
            if self.version >= (3, 0):
                build_configuration['ProjectExplorer.BuildConfiguration.BuildDirectory'] = project_build_conf.get(
                    'build_dir'
                )
            for bsl_num, bsl in enumerate(build_step_lists):
                bsl_key = 'ProjectExplorer.BuildConfiguration.BuildStepList.{}'.format(bsl_num)
                assert bsl_key in build_configuration
                build_configuration[bsl_key]['ProjectExplorer.BuildStepList.StepsCount'] = len(project_build_conf[bsl])
                for bs_num, bs_conf in enumerate(project_build_conf[bsl]):
                    assert 'id' in bs_conf
                    bs_key = 'ProjectExplorer.BuildStepList.Step.{}'.format(bs_num)
                    build_step = build_configuration[bsl_key].setdefault(bs_key, {})
                    build_step.update(
                        {
                            'ProjectExplorer.ProjectConfiguration.Id': bs_conf['id'],
                            'ProjectExplorer.BuildStep.Enabled': True,
                        }
                    )
                    build_step.update(bs_conf.get('params', {}))
            build_configuration.update(project_build_conf.get('extra_conf', {}))

        project_run_conf = self.project.run_conf_settings()
        logger.debug('Project run conf settings: %s', json.dumps(project_run_conf, sort_keys=True, indent=2))
        assert all(field in project_run_conf for field in ('output_dir',))

        for num, runnable in enumerate(sorted(six.iterkeys(self.project.runnables))):
            run_configuration = target.setdefault('ProjectExplorer.Target.RunConfiguration.{}'.format(num), {})
            run_configuration.update(
                {
                    'ProjectExplorer.ProjectConfiguration.Id': u'ProjectExplorer.CustomExecutableRunConfiguration',
                    'ProjectExplorer.ProjectConfiguration.DefaultDisplayName': exts.strings.to_unicode(runnable),
                    'ProjectExplorer.ProjectConfiguration.DisplayName': exts.strings.to_unicode(runnable),
                    'ProjectExplorer.CustomExecutableRunConfiguration.Executable': exts.strings.to_unicode(
                        self.project.runnables[runnable]
                    ),
                    'ProjectExplorer.CustomExecutableRunConfiguration.Arguments': u'',
                    'ProjectExplorer.CustomExecutableRunConfiguration.UseTerminal': False,
                    'ProjectExplorer.CustomExecutableRunConfiguration.WorkingDirectory': project_run_conf.get(
                        'output_dir'
                    ),
                    'RunConfiguration.UseCppDebugger': True,
                    'RunConfiguration.UseCppDebuggerAuto': False,
                    'RunConfiguration.UseQmlDebugger': False,
                    'RunConfiguration.UseQmlDebuggerAuto': False,
                }
            )

        yalibrary.qxml.save(
            self.settings_path,
            settings,
            'qtcreator',
            doctype='QtCreatorProject',
            comment='Arcadia QtCreator settings file generated by ya ide qt',
        )

    def _reset(self):
        project_directory = os.path.dirname(self.project.project_path)
        project_filename = os.path.basename(self.project.project_path)
        user_settings_filename = project_filename + '.user'
        for name in os.listdir(project_directory):
            if not name.startswith(user_settings_filename):
                continue
            user_settings_path = os.path.join(project_directory, name)
            logger.debug('Resetting user project settings file: %s', user_settings_path)
            exts.fs.remove_file(user_settings_path)


def gen_qt_project(params):
    import app_ctx  # XXX

    app_ctx.display.emit_message('[[bad]]ya ide qt is deprecated, please use clangd-based tooling instead')
    config = QtConfig(params, app_ctx)
    if params.install:
        app_ctx.display.emit_message('[[imp]]Installing required QtCreator configuration[[rst]]')
        config.install()
        app_ctx.display.emit_message(
            '[[good]]QtCreator configuration upgraded, now you may run project generation[[rst]]'
        )
        return
    app_ctx.display.emit_message('[[imp]]Generating QtCreator project[[rst]]')
    if params.project_type == 'cmake':
        project_creator = QtCMakeProject
    else:
        raise IdeQtError('QtCreator generation mode not implemented: {}'.format(params.project_type))
    project_info = ide.ide_common.IdeProjectInfo(
        params, app_ctx, instance_per_title=True, default_output_name=DEFAULT_QT_OUTPUT_DIR
    )
    dev_env = QtDevEnv(params, app_ctx, config, project_info, project_creator)
    dev_env.generate()
    run_cmd = 'ya ide qt --run'
    if project_info.title != ide.ide_common.DEFAULT_PROJECT_TITLE:
        run_cmd += ' -T ' + project_info.title
    if project_info.output_path != ide.ide_common.IdeProjectInfo.output_path_from_default(
        params, DEFAULT_QT_OUTPUT_DIR
    ):
        run_cmd += ' -P ' + project_info.output_path
    app_ctx.display.emit_message(
        '[[good]]Ready. Project created: {}. To start IDE use: {}[[rst]]'.format(dev_env.project.project_path, run_cmd)
    )


def run_qtcreator(params):
    logger.warning("ya ide qt is deprecated")
    import app_ctx

    fake_app_ctx = ide.ide_common.FakeAppCtx()

    project_info = ide.ide_common.IdeProjectInfo(
        params, fake_app_ctx, instance_per_title=True, default_output_name=DEFAULT_QT_OUTPUT_DIR
    )
    project_storage = ide.ide_common.IdeProjectStorage(project_info)
    if len(project_storage.data) == 0:
        raise ide.ide_common.RemoteIdeError('There\'s no such project: {}'.format(project_info.title))
    target = project_storage.data['project_runnable']
    if not os.path.exists(target):
        raise ide.ide_common.RemoteIdeError('Can\'t locate project: file {} doesn\'t exist'.format(target))
    path_to_ide = params.ide_path or project_storage.data['ide_path']
    if not path_to_ide:
        raise ide.ide_common.RemoteIdeError(
            'Couldn\'t find Qt Creator installed :-( Please, set the binary path manually with -I option'
        )
    app_ctx.display.emit_message('Using IDE: ' + path_to_ide)
    command = [path_to_ide, target]
    if project_storage.data['settings_path']:
        project_settings = project_storage.data['settings_path']
        command += ['-settingspath', project_settings]
    logger.debug('Running command: %s', ' '.join(command))
    if not params.daemonize:
        exts.process.execve(command[0], command[1:])
    new_env = os.environ.copy()
    exts.tmp.revert_tmp_dir(new_env)
    exts.process.popen(command, close_fds=True, env=new_env)
