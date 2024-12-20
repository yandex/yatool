import os

import jbuild.gen.consts as consts
import yalibrary.graph.commands as graph_commands


class BuildTools(object):  # TODO: use something from yalibrary.tools to detect executable file path
    PYTHON_PATTERN = 'PYTHON'
    MAVEN_ARTIFACT_UPLOADER = 'MAVEN_ARTIFACT_UPLOADER'
    KYTHE_PATTERN = 'KYTHE'
    KYTHE2PROTO_PATTERN = 'KYTHETOPROTO'

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
    def error_prone_tool(error_prone_resource):
        return (
            error_prone_resource
            if error_prone_resource.endswith('.jar')
            else os.path.join(error_prone_resource, 'error_prone.jar')
        )

    @staticmethod
    def jstyle_runner_dir(jstyle_resource):
        return jstyle_resource

    @staticmethod
    def kythe_tool():
        return os.path.join('$({})'.format(BuildTools.KYTHE_PATTERN), 'kythe')

    @staticmethod
    def kythe_to_proto_tool():
        return os.path.join('$({})'.format(BuildTools.KYTHE2PROTO_PATTERN), 'kythe_to_proto')

    @staticmethod
    def uberjar_tool(uberjar_resource):
        return (
            uberjar_resource
            if uberjar_resource.endswith('.jar')
            else os.path.join(uberjar_resource, 'devtools-java_shader.jar')
        )

    @staticmethod
    def jacoco_agent_tool(jacoco_agent_resource):
        return (
            jacoco_agent_resource
            if jacoco_agent_resource.endswith('.jar')
            else os.path.join(jacoco_agent_resource, 'devtools-jacoco-agent.jar')
        )

    @staticmethod
    def kotlin_compiler_tool(kotlin_compiler_resource):
        return (
            kotlin_compiler_resource
            if kotlin_compiler_resource.endswith('.jar')
            else os.path.join(kotlin_compiler_resource, 'kotlin-compiler.jar')
        )

    @staticmethod
    def groovy_compiler_tool(groovy_compiler_resource):
        return (
            groovy_compiler_resource
            if groovy_compiler_resource.endswith('groovyc')
            else os.path.join(groovy_compiler_resource, 'bin', 'groovyc')
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


def show(f, cwd=None):
    code = "import sys\n" "sys.stderr.write(sys.argv[1] + ': ' + open(sys.argv[1]).read())"

    cmd = [BuildTools.python(), '-c', code, f]

    return graph_commands.Cmd(cmd, cwd, [])


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


def copy_files(files_list, src_root, dest_root, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        'copy_files',
        src_root,
        dest_root,
        files_list,
    ]

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fs_tools.py'),
        ],
    )


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


def move_what_matches(src, dest, includes, excludes, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'devtools', 'ya', 'jbuild', 'gen', 'actions', 'move_matches.py'),
        '--src',
        src,
        '--dest',
        dest,
    ]

    if includes:
        cmd += ['--includes', ':'.join(includes)]

    if excludes:
        cmd += ['--excludes', ':'.join(excludes)]

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'devtools', 'ya', 'jbuild', 'gen', 'actions', 'move_matches.py'),
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


def fetch_resource(resource_id, path, custom_fetcher=None, verbose=False, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fetch_from_sandbox.py'),
        '--resource-id',
        resource_id,
        '--resource-file',
        '$(RESOURCE_ROOT)/sbr/{}/resource'.format(resource_id),
        '--copy-to',
        path,
    ]

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fetch_from_sandbox.py'),
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fetch_from.py'),
        ],
        resources=[{"uri": "sbr:{}".format(resource_id)}],
    )


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


def curl(
    url, path=None, follow_redirects=False, silent=True, retries=10, retry_delay=2, timeout=60, max_time=450, cwd=None
):
    cmd = ['/usr/bin/curl', '-f', url]
    if follow_redirects:
        cmd += ['-L']
    if silent:
        cmd += ['-sS']
    if retries:
        cmd += ['--retry', str(retries)]
    if retry_delay:
        cmd += ['--retry-delay', str(retry_delay)]
    if timeout:
        cmd += ['--connect-timeout', str(timeout)]
    if max_time:
        cmd += ['--max-time', str(max_time)]
    if path:
        cmd += ['-o', path]
    else:
        cmd += ['-O']
    return graph_commands.Cmd(cmd, cwd, [])


def make_manifest_from_buildfile(buildfile, manifest, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'make_manifest_from_bf.py'),
        buildfile,
        manifest,
    ]
    return graph_commands.Cmd(cmd, cwd, [])


def kythe_to_proto(entries, out_name, build_file, kythe_to_proto_tool, source_root=consts.SOURCE_ROOT, cwd=None):
    script_path = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'gen_java_codenav_protobuf.py')
    cmd = [
        BuildTools.python(),
        script_path,
        '--kythe-to-proto',
        kythe_to_proto_tool if kythe_to_proto_tool else BuildTools.kythe_to_proto_tool(),
        '--entries',
        entries,
        '--out-name',
        out_name,
        '--build-file',
        build_file,
        '--source-root',
        source_root,
    ]
    return graph_commands.Cmd(cmd, cwd, [script_path])


def merge_files(out, files, cwd=None):
    script_path = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'merge_files.py')
    cmd = [
        BuildTools.python(),
        script_path,
        out,
    ] + files
    return graph_commands.Cmd(cmd, cwd, [script_path])


def make_cp_file(src, dst, cwd=None):
    if not cwd:
        cwd = consts.BUILD_ROOT
    script_path = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'make_java_classpath_file.py')
    cmd = [BuildTools.python(), script_path, src, dst]
    return graph_commands.Cmd(cmd, cwd, [script_path])
