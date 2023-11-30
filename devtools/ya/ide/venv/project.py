import logging
import os

import build.build_handler
import core.yarg
import exts.fs
import yalibrary.makelists
import yalibrary.makelists.macro_definitions


PEER_PROJECT_TYPES = {
    'PROTO_LIBRARY',
    'PY23_LIBRARY',
    'PY3_LIBRARY',
    'SANDBOX_PY3_TASK',
    'PY3_PROGRAM',
}
TEST_PEER_PROJECT_TYPES = {
    'PY23_TEST',
    'PY3TEST',
}

logger = logging.getLogger(__name__)


class ProjectError(Exception):
    mute = True


class Project(object):
    def __init__(self, params, app_ctx, output_path, exe_name='yapython'):
        self.params = params

        self.project = params.venv_tmp_project
        self.source_path = os.path.join(params.arc_root, self.project)
        self.app_ctx = app_ctx
        self.exe_name = exe_name
        self.output_path = output_path

    def __enter__(self):
        if os.path.exists(self.source_path):
            raise ProjectError(
                '{} already exists. Use `--venv-tmp-project` option or `venv_tmp_project` ya.conf setting to set temporary project location'.format(
                    self.source_path
                )
            )
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        exts.fs.remove_tree_safe(os.path.join(self.source_path))

    @property
    def exe_path(self):
        return os.path.join(self.output_path, self.project, self.exe_name)

    def prepare(self):
        program = yalibrary.makelists.macro_definitions.Project('PY3_PROGRAM')
        program.add_value(self.exe_name)

        peers = self._discover_peers()
        if not peers:
            raise ProjectError("No python3 projects found in {}".format(", ".join(self.params.rel_targets)))
        peerdir = yalibrary.makelists.macro_definitions.Peerdir()
        peerdir.add('contrib/python/pip')
        for peer in sorted(peers):
            peerdir.add(peer)
        program.append_child(peerdir)

        main = yalibrary.makelists.macro_definitions.Macro.create_node('PY_MAIN')
        main.add_value(':main')
        program.append_child(main)

        resource = yalibrary.makelists.macro_definitions.Resource()
        resource.add_value('-')
        resource.add_value('YA_IDE_VENV=1')
        program.append_child(resource)

        end = yalibrary.makelists.macro_definitions.Macro.create_node('END')
        program.append_child(end)

        makelist = yalibrary.makelists.macro_definitions.MakeList()
        makelist.append_child(program)

        arc_project = yalibrary.makelists.ArcProject(self.params.arc_root, self.project)
        exts.fs.ensure_dir(os.path.join(self.params.arc_root, self.project))
        arc_project.write_makelist(makelist)
        makefile_path = arc_project.makelistpath()
        assert makefile_path
        logger.debug('Makefile: %s\n%s', makefile_path, open(makefile_path, 'rb').read())

    def build(self):
        build_params = core.yarg.Params(**self.params.as_dict())
        build_params.abs_targets = [self.source_path]
        build_params.output_root = self.output_path
        build_params.flags['YA_IDE_VENV'] = 'yes'
        build_params.flags['CHECK_INTERNAL'] = 'no'
        build_params.flags['EXCLUDE_SUBMODULES'] = 'PY3TEST_PROGRAM'
        if self.params.venv_excluded_peerdirs:
            build_params.flags['EXCLUDED_PEERDIRS'] = ' '.join(self.params.venv_excluded_peerdirs)
        exit_code = build.build_handler.do_ya_make(build_params)
        if exit_code:
            raise ProjectError('Python interpreter build failed with exist code={}'.format(exit_code))
        assert os.path.isfile(self.exe_path), 'Cannot find executable: {}'.format(self.exe_name)

    def _discover_peers(self):
        projects = []
        project_types = PEER_PROJECT_TYPES
        if self.params.venv_add_tests:
            project_types.update(TEST_PEER_PROJECT_TYPES)

        def walker(path):
            arc_project = yalibrary.makelists.ArcProject(self.params.arc_root, path)
            if arc_project.makelist().project.name in project_types:
                projects.append(path)

        arcadia = yalibrary.makelists.Arcadia(self.params.arc_root)
        arcadia.walk_projects(
            walker, visit_peerdirs=False, visit_tests=self.params.venv_add_tests, paths=self.params.rel_targets
        )
        return projects
