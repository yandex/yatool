import os

import jbuild.gen.consts as consts
import yalibrary.graph.commands as graph_commands


class BuildTools(object):  # TODO: use something from yalibrary.tools to detect executable file path
    PYTHON_PATTERN = 'PYTHON'
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
        return os.path.join(os.path.join('$({})'.format(BuildTools.PYTHON_PATTERN), 'python'))

    @staticmethod
    def jacoco_agent_tool(jacoco_agent_resource):
        return (
            jacoco_agent_resource
            if jacoco_agent_resource.endswith('.jar')
            else os.path.join(jacoco_agent_resource, 'devtools-jacoco-agent.jar')
        )


def resolve_java_srcs(
    dir,
    sources_list,
    kotlins_list,
    groovy_list,
    others_list,
    include_patterns,
    exclude_patterns=None,
    cwd=None,
    resolve_kotlin=False,
    resolve_groovy=False,
):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'devtools', 'ya', 'jbuild', 'resolve_java_srcs.py'),
        '-d',
        dir,
        '-s',
        sources_list,
        '-k',
        kotlins_list,
        '-g',
        groovy_list,
        '-r',
        others_list,
    ]
    if resolve_kotlin:
        cmd += ['--resolve-kotlin']
    if resolve_groovy:
        cmd += ['--resolve-groovy']

    cmd += ['--include-patterns'] + include_patterns

    if exclude_patterns:
        cmd += ['--exclude-patterns'] + exclude_patterns

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'devtools', 'ya', 'jbuild', 'resolve_java_srcs.py'),
        ],
    )


def collect_java_srcs(root, files, dest, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'collect_java_srcs.py'),
        consts.BUILD_ROOT,
        root,
        dest,
    ] + files

    return graph_commands.Cmd(cmd, cwd, [os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'collect_java_srcs.py')])


def prepare_build_file(files, srcdirs, out, cwd=None, splitter=' '):
    code = (
        "import sys\n"
        "import os\n"
        "o = sys.argv[1]\n"
        "n = int(sys.argv[2])\n"
        "fs = sys.argv[3: 3 + n]\n"
        "ss = sys.argv[3 + n: 3 + 2 * n]\n"
        "ps = []\n"
        "for f, s in zip(fs, ss):\n"
        "    ps.extend([os.path.normpath(os.path.join(s, x)) for x in open(f).read().strip().split()])\n"
        "open(o, 'w').write('{}'.join(ps))\n".format(splitter)
    )

    cmd = [BuildTools.python(), '-c', code, out, str(len(files))] + files + srcdirs

    return graph_commands.Cmd(cmd, cwd, [])


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


def cp(source_path, dest_path, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        'copy',
        source_path,
        dest_path,
    ]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
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


def copy_all_files(source_path, dest_path, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        'copy_all_files',
        source_path,
        dest_path,
    ]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        ],
    )


def touch(path, cwd=None):
    cmd = [BuildTools.python(), os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'touch.py'), path]
    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'touch.py'),
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
