import os

import devtools.ya.test.common as test_common
import devtools.ya.jbuild.gen.consts as consts
import yalibrary.graph.commands as graph_commands


class BuildTools(object):  # TODO: use something from yalibrary.tools to detect executable file path
    PYTHON_PATTERN = None
    MAVEN_ARTIFACT_UPLOADER = 'MAVEN_ARTIFACT_UPLOADER'
    JSTYLE_RUNNER_PATTERN = 'JSTYLERUNNER'

    @staticmethod
    def maven_artifact_uploader():
        return os.path.join('$({})'.format(BuildTools.MAVEN_ARTIFACT_UPLOADER), 'uploader')

    @staticmethod
    def jdk_tool(name, jdk_path):
        return os.path.join(jdk_path, 'bin', name)

    @staticmethod
    def python():
        if BuildTools.PYTHON_PATTERN is None:
            BuildTools.PYTHON_PATTERN = test_common.get_python_cmd()[0]
        return BuildTools.PYTHON_PATTERN


def move_if_exists(src, dest, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        'rename_if_exists',
        src,
        dest,
    ]

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        ],
    )


def append(file_path, content, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'writer.py'),
        '--file',
        file_path,
        '--content',
        content,
        '--append',
    ]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'writer.py'),
        ],
    )


def mkdir(path, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'mkdir.py'),
        path,
    ]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'mkdir.py'),
        ],
    )


def mv(s, d, cwd=None):
    cmd = [BuildTools.python(), os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'), 'rename', s, d]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        ],
    )


def rm(path, cwd=None):
    cmd = [BuildTools.python(), os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'), 'remove', path]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        ],
    )


def jar(files, archive_path, jdk_resource, manifest=None, verbose=True, cwd=None, update=False):
    cmd = [BuildTools.jdk_tool('jar', jdk_path=jdk_resource), 'uf' if update else 'cf', archive_path]
    if verbose:
        cmd[1] += 'v'
    if manifest:
        cmd[1] += 'm'
        cmd += [manifest]
    elif not update:
        cmd[1] += 'M'
    if files:
        cmd += files
    return graph_commands.Cmd(cmd, cwd, [])


def jarx(archive_path, jdk_resource, verbose=True, cwd=None):  # XXX
    cmd = [BuildTools.jdk_tool('jar', jdk_path=jdk_resource), 'xf', archive_path]
    if verbose:
        cmd[1] += 'v'
    return graph_commands.Cmd(cmd, cwd, [])


def upload_resource(resource_path, id_path, resource_type=None, owner=None, token=None, transport=None, cwd=None):
    cmd = [
        BuildTools.maven_artifact_uploader(),
        '--artifact-path',
        resource_path,
        '--resource-id-path',
        id_path,
    ]

    if resource_type:
        cmd += ['--resource-type', resource_type]

    if owner:
        cmd += ['--owner', owner]

    if token:
        cmd += ['--token', token]

    if transport:
        cmd += ['--transport', transport]

    return graph_commands.Cmd(cmd, cwd, [])
