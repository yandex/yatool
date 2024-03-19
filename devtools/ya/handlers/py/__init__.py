from __future__ import absolute_import
import collections
import logging
import os
import tempfile
from contextlib import contextmanager

import library.python.filelock
import library.python.resource

import build.build_handler
import build.build_opts
import core.common_opts
import core.config
import core.yarg
import exts.fs
import exts.hashing
import exts.process
from exts.windows import on_win
import yalibrary.makelists
import yalibrary.makelists.macro_definitions
import yalibrary.makelists.mk_common

import app


logger = logging.getLogger(__name__)


class PyYaHandler(core.yarg.OptsHandler):
    def __init__(self):
        super(PyYaHandler, self).__init__(
            action=app.execute(run),
            description='Run IPython shell with python libraries baked in',
            opts=build.build_opts.ya_make_options(free_build_targets=True) + [
                PyOptions(),
            ],
        )


class PyOptions(core.yarg.Options):
    def __init__(self):
        self.py3 = True
        self.py_bare = False
        self.py_tmp_project = os.path.join('junk', core.config.get_user(), '_ya_py')
        self.kernel_connection_file = None

    @staticmethod
    def consumer():
        return [
            core.yarg.ArgConsumer(
                ['-2', '--py2'],
                help='Build with Python 2',
                hook=core.yarg.SetConstValueHook('py3', False),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-3', '--py3'],
                help='Build with Python 3',
                hook=core.yarg.SetConstValueHook('py3', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['-b', '--py-bare'],
                help='Bare build (without additional projects)',
                hook=core.yarg.SetConstValueHook('py_bare', True),
                group=core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            core.yarg.ArgConsumer(
                ['--py-tmp-project'],
                help='Temporary project path',
                hook=core.yarg.SetValueHook('py_tmp_project'),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
            core.yarg.ConfigConsumer(
                name='py_tmp_project',
                hook=core.yarg.SetValueHook('py_tmp_project'),
            ),
            core.yarg.ArgConsumer(
                ['--py-jupyter-kernel-connection-file'],
                help='Run python in jupyter kernel mode with the given connection file',
                hook=core.yarg.SetValueHook('kernel_connection_file'),
                group=core.yarg.ADVANCED_OPT_GROUP,
            ),
        ]


class Error(Exception):
    mute = True


def run(params):
    import app_ctx

    env = os.environ.copy()

    cache = os.path.join(core.config.misc_root(), 'py')
    locks = exts.fs.create_dirs(os.path.join(cache, 'locks'))
    binary = os.path.join(cache, 'yapy')
    logger.debug('Cache: %s', cache)

    with tmp_project(params, app_ctx) as project:
        logger.debug('Project: %s', project.project)
        logger.debug('Source: %s', project.source_path)
        logger.debug('Output: %s', project.output_path)
        logger.debug('Exe: %s', project.exe_path)

        lock_path = os.path.join(locks, '{}.lock'.format(exts.hashing.md5_value(project.source_path)))
        with library.python.filelock.FileLock(lock_path):
            if os.path.exists(project.source_path):
                project.cleanup()
            targets = project.prepare()
            if targets:
                msg = ['Baking shell with:']
                progs = sorted(target for target in targets if target.program)
                libs = sorted(target for target in targets if not target.program)
                if progs:
                    assert len(progs) == 1
                    msg.append(' {} (program)'.format(progs[0].path))
                for lib in libs:
                    msg.append(' {}'.format(lib.path))
                app_ctx.display.emit_message('[[good]]{}[[rst]]'.format('\n'.join(msg)))
            else:
                app_ctx.display.emit_message('[[good]]Baking bare shell[[rst]]')
            project.build()

        exts.fs.replace_file(project.exe_path, binary)

    if params.kernel_connection_file:
        env['Y_PYTHON_ENTRY_POINT'] = ':main'
        args = ['-m', 'ipykernel_launcher', '-f', params.kernel_connection_file]
    else:
        env['Y_PYTHON_ENTRY_POINT'] = ':repl'
        args = []

    exts.process.execve(binary, env=env, args=args)


class Project(object):
    def __init__(self, params, app_ctx):
        self.params = params
        self.app_ctx = app_ctx
        self.project = params.py_tmp_project
        self.source_path = os.path.join(params.arc_root, self.project)
        self.output_path = tempfile.mkdtemp(prefix='ya_py-')
        self.exe_name = 'yapy' + ('.exe' if on_win() else '')
        self.targets = set(params.rel_targets) if not params.py_bare else set()

    @property
    def exe_path(self):
        return os.path.join(self.output_path, self.project, self.exe_name)

    def prepare(self):
        if os.path.exists(self.source_path):
            raise Error('Temporary project path is occupied ({path}), use `--py-tmp-project` option or `py_tmp_project` ya.conf setting to set temporary project location'.format(
                path=self.project,
            ))

        py3 = self.params.py3
        targets = set()
        for target_path in self.targets:
            logger.debug('Target: %s', target_path)
            target = _parse_target(target_path, self.params.arc_root)
            if not target:
                continue
            if py3 is None and target.makefile.project.name.startswith('PY3'):
                py3 = True
            targets.add(target)

        programs = set(target.path for target in targets if target.program)
        if len(programs) > 1:
            raise Error('Multiple programs baking is not supported: {}'.format(' '.join(sorted(programs))))

        unit_type = 'PY3_PROGRAM' if py3 else 'PY2_PROGRAM'
        program = None
        deps = set(('contrib/python/ipython',))
        for target in targets:
            if target.program:
                program = target
            else:
                deps.add(target.path)

        if program:
            makefile = program.makefile
            try:
                self.exe_name = _clone_program(makefile, deps, target.path)
            except WeirdMakefileError as e:
                e.args = tuple('Weird makefile in {}: {}'.format(target.path, str(e)),)
                raise
        else:
            makefile = yalibrary.makelists.macro_definitions.MakeList()
            _gen_program(makefile, unit_type, deps)

        self.app_ctx.display.emit_message('Creating temporary {} {}'.format(unit_type, self.source_path))
        exts.fs.ensure_dir(self.source_path)
        arc_project = yalibrary.makelists.ArcProject(self.params.arc_root, self.project)
        exts.fs.ensure_dir(os.path.join(self.params.arc_root, self.project))
        arc_project.write_makelist(makefile)
        makefile_path = arc_project.makelistpath()
        assert makefile_path
        with open(makefile_path, 'rb') as f:
            makefile_content = f.read()

        logger.debug('Makefile: %s\n%s', makefile_path, makefile_content)

        return targets

    def build(self):
        build_params = core.yarg.Params(**self.params.as_dict())
        build_params.abs_targets = [self.source_path]
        build_params.output_root = self.output_path
        build.build_handler.do_ya_make(build_params)
        assert os.path.isfile(self.exe_path), 'Cannot find executable: {}'.format(self.exe_path)

    def cleanup(self):
        exts.fs.remove_tree_safe(self.source_path)


@contextmanager
def tmp_project(params, app_ctx):
    proj = Project(params, app_ctx)
    try:
        yield proj
    finally:
        proj.cleanup()


class _Target(collections.namedtuple('Target', ('path', 'makefile', 'program'))):
    def __new__(cls, path, makefile, program=False):
        return super(_Target, cls).__new__(cls, path, makefile, program)


def _parse_target(target, root):
    arc_project = yalibrary.makelists.ArcProject(root, target)
    if not arc_project.makelistpath():
        logger.warn('No makelist found for: %s', target)
        return None
    makefile = arc_project.makelist()
    try:
        project = makefile.project
    except yalibrary.makelists.mk_common.MkLibException:
        logger.warn('Can not get project for: %s', target)
        return None
    if project.name in ('LIBRARY', 'PROTO_LIBRARY', 'PY2_LIBRARY', 'PY23_LIBRARY', 'PY3_LIBRARY', 'SANDBOX_TASK', 'SANDBOX_PY3_TASK'):
        return _Target(target, makefile)
    elif project.name in ('PROGRAM', 'PY2_PROGRAM', 'PY3_PROGRAM', 'PY2TEST', 'PY3TEST', 'PY23_TEST'):
        return _Target(target, makefile, program=True)
    else:
        logger.warn('Unsupported project type \'%s\': %s', project.name, target)
        return None


def _gen_program(makefile, unit_type, deps):
    program = yalibrary.makelists.macro_definitions.Project(unit_type)
    program.add_value('yapy')

    peerdir = yalibrary.makelists.macro_definitions.Peerdir()
    for dep in sorted(deps):
        peerdir.add(dep)
    program.append_child(peerdir)

    main = yalibrary.makelists.macro_definitions.StringMacro('PY_MAIN')
    main.add_value('IPython:start_ipython')
    program.append_child(main)

    end = yalibrary.makelists.macro_definitions.StringMacro('END')
    program.append_child(end)

    makefile.append_child(program)


class WeirdMakefileError(Error):
    pass


def _clone_program(makefile, deps, srcdir_path):
    program = makefile.project
    names = program.get_values()
    if not names:
        program.add_value('yapy')
        name = 'yapy'
    elif len(names) == 1:
        name = names[0].name
    else:
        raise WeirdMakefileError('complex name {}'.format(' '.join(x.name for x in names)))

    nodes_end = program.find_nodes('END')
    if len(nodes_end) != 1:
        raise WeirdMakefileError('cannot find END macro')
    end = nodes_end[0]

    if not program.find_nodes('SRCDIR'):
        srcdir = yalibrary.makelists.macro_definitions.StringMacro('SRCDIR')
        srcdir.add_value(srcdir_path)
        program.insert_before(srcdir, end)

    for recurse in makefile.find_nodes('RECURSE'):
        makefile.remove_child(recurse)

    peerdir = yalibrary.makelists.macro_definitions.Peerdir()
    for dep in sorted(deps):
        peerdir.add(dep)
    program.insert_before(peerdir, end)

    return name
