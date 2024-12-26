from __future__ import absolute_import
import configparser
import logging
import os
import six
import stat

import devtools.ya.core.common_opts
import devtools.ya.core.config
import exts.fs
import exts.path2
import exts.process
import devtools.ya.ide.ide_common
from . import qt

logger = logging.getLogger(__name__)


def _shell_quote(s):
    if six.PY3:
        import shlex

        return shlex.quote(s)
    else:
        import pipes

        return pipes.quote(s)


class QtRemoteDevEnv(object):
    def __init__(self, params, app_ctx, project_info):
        self.params = params
        self.app_ctx = app_ctx
        self.project_info = project_info
        qt_config_path = os.path.join(self.project_info.instance_path, qt.QT_REMOTE_SETTINGS_DIR)
        gdbwrapper = os.path.join(qt_config_path, 'gdbwrapper')
        if self.params.reset:
            self._prepare_qtcreator_config(qt_config_path, gdbwrapper)
        self.config = qt.QtConfig(
            params, self.app_ctx, path=os.path.join(qt_config_path, 'QtProject'), gdb_path=gdbwrapper
        )
        self.config.install()
        self.qt_dev_env = qt.QtDevEnv(
            params, self.app_ctx, self.config, self.project_info, QtRemoteProject, verify_config=False
        )
        remote_params = {
            'remote_host': self.params.remote_host,
        }
        sshp = devtools.ya.ide.ide_common.SSHProxy(self.params.remote_host)
        self.remote_source_path = sshp.get_real_path(
            os.path.join(self.params.remote_cache_path, devtools.ya.ide.ide_common.REMOTE_SOURCE_SUBDIR)
        )
        self.remote_build_root = os.path.normpath(
            os.path.join(self.remote_source_path, '..', devtools.ya.ide.ide_common.REMOTE_BUILD_SUBDIR, 'build_root')
        )
        remote_params['remote_path'] = self.remote_source_path
        self.project_storage = self.qt_dev_env.project_storage
        self.remote_config = self.project_storage.data['remote_params'] = remote_params
        self.project_storage.data['project_runnable'] = self.qt_dev_env.project.project_path
        self.project_storage.data['settings_path'] = qt_config_path
        self.sync_files = None
        self.exclude_sync = (r'^(.*{0})?\.svn{0}.*'.format(os.path.sep), r'.*\.autosave')

    def _prepare_qtcreator_config(self, qt_config_path, gdbwrapper):
        self.app_ctx.display.emit_message('QtCreator internal config: {}'.format(qt_config_path))
        exts.fs.ensure_removed(qt_config_path)
        exts.fs.copy_tree(os.path.join(qt.discover_qt_config()), os.path.join(qt_config_path, 'QtProject'))
        script_text = '#!/bin/sh -e\n' + 'exec {0} remote_gdb --host {1} --remote-cache {2} -- "$@"\n'.format(
            _shell_quote(devtools.ya.core.config.ya_path(self.params.arc_root, 'ya')),
            self.params.remote_host,
            self.params.remote_cache_path,
        )
        exts.fs.write_file(gdbwrapper, script_text)
        os.chmod(gdbwrapper, os.stat(gdbwrapper).st_mode | stat.S_IEXEC)

    @staticmethod
    def _path_to_regex(path):
        return path.replace('\\', '\\\\').replace('.', r'\.')

    def _add_debug_info(self, config, binaries):
        section_name = 'DebugMode'
        size_option = r'StartApplication\size'
        if not config.has_section(section_name):
            # very unlikely
            config.add_section(section_name)
        new_options = [
            [r'StartApplication\{size}\LastDebugInfoLocation', ''],
            [r'StartApplication\{size}\LastExternalBreakAtMain', 'true'],
            [r'StartApplication\{size}\LastExternalExecutable', '{abs_executable}'],
            [r'StartApplication\{size}\LastExternalExecutableArguments', ''],
            [r'StartApplication\{size}\LastExternalRunInTerminal', 'false'],
            [r'StartApplication\{size}\LastExternalWorkingDirectory', ''],
            [r'StartApplication\{size}\LastKitId', 'Ya.Ide.Qt.Kit'],
            [r'StartApplication\{size}\LastServerAddress', '{connect_command}'],
            [r'StartApplication\{size}\LastServerPort', '12345'],
            [r'StartApplication\{size}\LastServerStartScript', ''],
        ]
        if config.has_option(section_name, size_option):
            count = config.getint(section_name, size_option)
        else:
            count = 0

        for bin_path in binaries:
            executable_path = os.path.join(self.qt_dev_env.project.output_conf_path('Debug'), bin_path)
            output_path = os.path.join(self.params.remote_cache_path, devtools.ya.ide.ide_common.REMOTE_OUTPUT_SUBDIR)
            connect_command = '!{0}!{1}!'.format(
                self.params.remote_host,
                os.path.join(output_path, bin_path),
            )
            dct = {
                'run_script': '',
                'abs_executable': executable_path,
                'connect_command': connect_command,
                'size': count + 1,
            }
            for opt in new_options:
                config.set(section_name, opt[0].format(**dct), opt[1].format(**dct))
            count += 1
        config.set(section_name, size_option, str(count))

        def finish_with_slash(lst):
            return lst if lst.endswith('/') else lst + '/'

        source_path = finish_with_slash(self.remote_source_path)
        local_source_path = finish_with_slash(self.params.arc_root)
        build_path = finish_with_slash(self.remote_build_root)
        # assume this is the only configuration that generates debug symbols
        local_output_path = finish_with_slash(self.qt_dev_env.project.output_conf_path('Debug'))
        section_name = 'SourcePathMappings'
        if not config.has_section(section_name):
            config.add_section(section_name)
        if config.has_option(section_name, 'size'):
            size = config.getint(section_name, 'size')
        else:
            size = 0
        config.set(section_name, r'{}\Source'.format(size + 1), source_path)
        config.set(section_name, r'{}\Target'.format(size + 1), local_source_path)
        config.set(section_name, r'{}\Source'.format(size + 2), '#' + build_path)
        config.set(section_name, r'{}\Target'.format(size + 2), local_output_path)
        config.set(section_name, 'size', str(size + 2))

    @staticmethod
    def _write_special_ini(config, settings_file):
        ini_buffer = six.StringIO()
        config.write(ini_buffer)
        ini_buffer.seek(0)
        output = [x.replace(' = ', '=', 1) for x in ini_buffer.readlines()]
        with open(settings_file, 'wt') as sf:
            sf.write(''.join(output))

    def _modify_qtcreator_config(self):
        config_file = os.path.join(self.config.path, 'QtCreator.ini')
        config = configparser.ConfigParser()
        config.optionxform = str
        with open(config_file) as sf:
            config.readfp(sf)
        self._add_debug_info(config, list(self.qt_dev_env.project.cmake_stub.binaries.values()))
        self._write_special_ini(config, config_file)

    def generate(self):
        self.qt_dev_env.generate()
        self.sync_files = self.qt_dev_env.project.cmake_stub.ide_graph.sync_targets(
            self.params.arc_root, add_extra=self.params.source_mine_hacks, exclude_regexps=self.exclude_sync
        )
        with open(
            os.path.join(self.project_info.instance_path, devtools.ya.ide.ide_common.SYNC_LIST_FILE_NAME), 'w'
        ) as sync_list_file:
            sync_list_file.write('\n'.join(self.sync_files))
        if self.params.reset:
            self._modify_qtcreator_config()


def generate_remote_project(params):
    logger.warning("ya ide qt is deprecated")
    import app_ctx

    app_ctx.display.emit_message('[[imp]]Generating Remote IDE project[[rst]]')

    devtools.ya.ide.ide_common.set_up_remote(params, app_ctx)

    project_info = devtools.ya.ide.ide_common.IdeProjectInfo(
        params, app_ctx, instance_per_title=True, default_output_name=qt.DEFAULT_QT_OUTPUT_DIR
    )
    dev_env = QtRemoteDevEnv(params, app_ctx, project_info)
    dev_env.generate()
    run_cmd = 'ya ide qt --run'
    if project_info.title != devtools.ya.ide.ide_common.DEFAULT_PROJECT_TITLE:
        run_cmd += ' -T ' + project_info.title
    if project_info.output_path != devtools.ya.ide.ide_common.IdeProjectInfo.output_path_from_default(
        params, qt.DEFAULT_QT_OUTPUT_DIR
    ):
        run_cmd += ' -P ' + project_info.output_path
    app_ctx.display.emit_message('[[good]]Ready. Project created. To start Remote IDE use: {}[[rst]]'.format(run_cmd))


class QtRemoteProject(qt.QtCMakeProject):
    def __init__(self, params, *args, **kwargs):
        super(QtRemoteProject, self).__init__(
            params,
            *args,
            selected_targets=('REMOTE_BUILD_AND_DOWNLOAD_' + devtools.ya.ide.ide_common.JOINT_TARGET_REMOTE_NAME,),
            **kwargs
        )
        self.cmake_file = self.cmake_stub.project_path

    def generate(self):
        super(QtRemoteProject, self).generate()
