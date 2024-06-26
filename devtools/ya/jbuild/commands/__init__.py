import six

import os
import base64
import exts.yjson as json

import jbuild.gen.consts as consts
import yalibrary.graph.commands as graph_commands


class BuildTools(object):  # TODO: use something from yalibrary.tools to detect executable file path
    PROTOC_PATTERN = 'PROTOC'
    PYTHON_PATTERN = 'PYTHON'
    YMAKE_PATTERN = 'YMAKE'
    MAVEN_ARTIFACT_UPLOADER = 'MAVEN_ARTIFACT_UPLOADER'
    JSTYLE_RUNNER_PATTERN = 'JSTYLERUNNER'
    KYTHE_PATTERN = 'KYTHE'
    KYTHE2PROTO_PATTERN = 'KYTHETOPROTO'
    UBERJAR_PATTERN = 'UBERJAR'
    UBERJAR10_PATTERN = 'UBERJAR_10'
    SCRIPTGEN_PATTERN = 'SCRIPTGEN'

    YMAKE_BIN = None

    @staticmethod
    def maven_artifact_uploader():
        return os.path.join('$({})'.format(BuildTools.MAVEN_ARTIFACT_UPLOADER), 'uploader')

    @staticmethod
    def jdk_tool(name, jdk_path):
        return os.path.join(jdk_path, 'bin', name)

    @staticmethod
    def protoc_tool(name='protoc'):
        return os.path.join('$({})'.format(BuildTools.PROTOC_PATTERN), name)

    @staticmethod
    def python():
        return os.path.join(os.path.join('$({})'.format(BuildTools.PYTHON_PATTERN), 'python'))

    @staticmethod
    def ymake():
        if BuildTools.YMAKE_BIN:
            return BuildTools.YMAKE_BIN

        else:
            return os.path.join('$({})'.format(BuildTools.YMAKE_PATTERN), 'ymake')

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
    def scriptgen_tool():
        return os.path.join('$({})'.format(BuildTools.SCRIPTGEN_PATTERN), 'scriptgen')

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


def run_jp(classpath, args, jdk_resource, libpath=None, cwd=None):
    if cwd is None:
        cwd = consts.BUILD_ROOT
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fix_java_command_file_cp.py'),
        '--build-root',
        consts.BUILD_ROOT,
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'setup_java_tmpdir.py'),
        BuildTools.jdk_tool('java', jdk_path=jdk_resource),
        '-Dfile.encoding=utf8',
    ]

    if libpath:
        cmd += ['--fix-path-sep', '-Djava.library.path=' + '::'.join(libpath)]
        cmd += ['--fix-path-sep', '-Djna.library.path=' + '::'.join(libpath)]

    cmd += ['-classpath', '--fix-path-sep', '::'.join(classpath)] + args

    cmd = [BuildTools.python(), os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'stdout2stderr.py')] + cmd

    return graph_commands.Cmd(
        cmd,
        cwd,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'fix_java_command_file_cp.py'),
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'stdout2stderr.py'),
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'setup_java_tmpdir.py'),
        ],
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


def run_test(
    classpath_file,
    tests_jar,
    source_root,
    jdk_resource,
    agent_disposition,
    build_root=None,
    sandbox_resources_root=None,
    test_outputs_root=None,
    output=None,
    filters=None,
    modulo=1,
    modulo_i=0,
    fork_subtests=False,
    list_tests=False,
    runner_log_path=None,
    tests_tmp_dir=None,
    libpath=None,
    allure=False,
    props=None,
    jvm_args=None,
    cwd=None,
    coverage=False,
    suite_work_dir=None,
    params=None,
    junit_args=None,
    ytrace_file=None,
    cmd_cp_type=None,
):
    properties = []
    if not classpath_file.endswith('.jar'):
        classpath_file = '@' + classpath_file

    if allure:
        allure_dir = os.path.join(suite_work_dir or os.curdir, "allure")
        properties.append('-Dallure.results.directory=' + allure_dir)

    if tests_tmp_dir:
        properties.append('-Djava.io.tmpdir=' + tests_tmp_dir)

    if libpath:
        properties.append('-Djava.library.path=' + ':'.join(libpath))
        properties.append('-Djna.library.path=' + ':'.join(libpath))

    if not jvm_args:
        jvm_args = []

    if not coverage or not suite_work_dir:
        coverage = []
    else:
        coverage = [
            '-javaagent:{}=output=file,destfile={}'.format(
                BuildTools.jacoco_agent_tool(agent_disposition),
                os.path.join(suite_work_dir, 'java.coverage', 'report.exec'),
            )
        ]

    cmd = [
        BuildTools.python(),
        consts.SOURCE_ROOT + '/build/scripts/run_junit.py',
        BuildTools.python(),
        consts.SOURCE_ROOT + '/build/scripts/unpacking_jtest_runner.py',
    ]
    if cmd_cp_type:
        cmd += ['--classpath-option-type', cmd_cp_type]

    if ytrace_file:
        cmd += ['--trace-file', ytrace_file]

    cmd += (
        [
            '--jar-binary',
            BuildTools.jdk_tool('jar', jdk_path=jdk_resource),
            '--tests-jar-path',
            tests_jar,
            BuildTools.jdk_tool('java', jdk_path=jdk_resource),
        ]
        + coverage
        + properties
        + jvm_args
        + [
            '-classpath',
            classpath_file,
            'ru.yandex.devtools.test.Runner',
            '--tests-jar',
            tests_jar,
            '--source-root',
            source_root,
            '--modulo',
            str(modulo),
            '--modulo-index',
            str(modulo_i),
        ]
    )

    if build_root:
        cmd.extend(['--build-root', build_root])

    if sandbox_resources_root:
        cmd.extend(['--sandbox-resources-root', sandbox_resources_root])

    if test_outputs_root:
        cmd.extend(['--test-outputs-root', test_outputs_root])

    if output:
        cmd.extend(['--output', output])

    if filters:
        for f in filters:
            cmd.extend(['--filter', f])

    if fork_subtests:
        cmd.append('--fork-subtests')

    if list_tests:
        cmd.append('--list')

    if runner_log_path:
        cmd.extend(['--runner-log-path', runner_log_path])

    if allure:
        cmd.append('--allure')

    if props:
        cmd.extend(['--properties', props])

    if params:
        for key, value in six.iteritems(params):
            cmd += ["--test-param", "{}={}".format(key, value)]

    if junit_args:
        cmd += junit_args

    return graph_commands.Cmd(cmd, cwd, [])


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


def protoc(proto_files, java_out, Ipaths=list(), cwd=None):
    cmd = [BuildTools.protoc_tool()]
    cmd += ['-I={}'.format(path) for path in Ipaths]
    cmd += ['--java_out={}'.format(java_out)]
    cmd += proto_files
    return graph_commands.Cmd(cmd, cwd, [])


def javac(
    source_files,
    jdk_resource,
    deps=None,
    out_dir=None,
    sourcepath=None,
    X=None,
    encoding=None,
    custom_flags=None,
    verbose=False,
    nowarn=False,
    cwd=None,
    use_error_prone=None,
    kythe_tool=None,
    out_jar_name=None,
    error_prone_flags=None,
):
    import jbuild.gen.actions.compile as jgac

    cmds = []
    error_prone_script = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'build_java_with_error_prone2.py')
    codenav_index_gen_script = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'build_java_codenav_index.py')
    if kythe_tool:
        jcmd = [
            BuildTools.python(),
            codenav_index_gen_script,
            BuildTools.jdk_tool('java', jdk_path=jdk_resource),
            os.path.join(BuildTools.kythe_tool(), 'extractors/javac_extractor.jar'),
            os.path.join(out_dir, out_jar_name),
            consts.BUILD_ROOT,
            consts.SOURCE_ROOT,
            source_files,
        ]
    elif use_error_prone:
        jcmd = [
            BuildTools.python(),
            error_prone_script,
            BuildTools.jdk_tool('java', jdk_path=jdk_resource),
            BuildTools.jdk_tool('javac', jdk_path=jdk_resource),
            BuildTools.error_prone_tool(use_error_prone),
        ] + (error_prone_flags or [])
    else:
        jcmd = [BuildTools.jdk_tool('javac', jdk_path=jdk_resource)]
    if nowarn:
        jcmd.append('-nowarn')

    jcmd += [
        '@' + os.path.join(os.path.dirname(source_files), ('_' if kythe_tool else '') + os.path.basename(source_files))
    ]

    flags = {'g': None}
    if deps:
        manifest_name = '$(BUILD_ROOT)/bfg.jar'
        bf_name = '$(BUILD_ROOT)/bfg.txt'
        if not kythe_tool:
            cmds += jgac.make_build_file(list(map(jgac.prepare_path_to_manifest, deps)), '\n', bf_name) + [
                make_manifest_from_buildfile(bf_name, manifest_name)
            ]
        jcmd += ['-classpath', manifest_name]

    if out_dir:
        flags['d'] = out_dir
    if sourcepath:
        flags['sourcepath'] = sourcepath
    if X:
        flags['X{}'.format(X)] = None
    if encoding:
        flags['encoding'] = encoding

    if custom_flags:
        flags.update(custom_flags)

    for k, v in six.iteritems(flags):
        jcmd.append('-{}'.format(k))

        if v:
            jcmd.append(v)

    if kythe_tool:
        jcmd += ['-s', out_dir]
        cmds += jgac.make_build_file(
            list(map(jgac.prepare_path_to_manifest, [os.path.join(out_dir, out_jar_name)] + (deps or []))),
            '\n',
            os.path.join(out_dir, 'bfg.txt'),
        )

    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'setup_java_tmpdir.py'),
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
        '--sources-list',
        source_files,
    ]
    if kythe_tool:
        cmd.append('--ignore-errors')

    if verbose:
        cmd.append('--verbose')

    if nowarn:
        cmd.append('--remove-notes')

    cmd.extend(jcmd)

    cmd_deps = [
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'setup_java_tmpdir.py'),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
    ]
    if use_error_prone:
        cmd_deps.append(error_prone_script)

    return cmds + [graph_commands.Cmd(cmd, cwd, cmd_deps)]


def kotlinc(
    source_files,
    jdk_resource,
    kotlin_complier_resource,
    module_name=None,
    deps=None,
    out_dir=None,
    custom_flags=None,
    verbose=False,
    nowarn=False,
    cwd=None,
    jvm_target=None,
    custom_opts=None,
):
    def resolve_compiler_resource(s):
        if s:
            return s.replace('$KOTLIN_COMPILER_RESOURCE_GLOBAL', kotlin_complier_resource).replace(
                '${KOTLIN_COMPILER_RESOURCE_GLOBAL}', kotlin_complier_resource
            )
        return s

    import jbuild.gen.actions.compile as jgac

    cmds = []
    kotlin_compiler_tool = BuildTools.kotlin_compiler_tool(kotlin_complier_resource)
    jcmd = [BuildTools.jdk_tool('java', jdk_path=jdk_resource), '-jar', kotlin_compiler_tool, '-no-stdlib']
    if nowarn:
        jcmd.append('-nowarn')
    if module_name:
        jcmd += ['-module-name', module_name]
    if jvm_target:
        jcmd += ['-jvm-target', jvm_target]

    jcmd += ['@' + source_files]

    flags = {}
    if deps:
        bf_name = '$(BUILD_ROOT)/kotlin_bfg.txt'
        cp_name = '$(BUILD_ROOT)/kotlin_cp.txt'
        cmds += jgac.make_build_file(list(map(jgac.prepare_path_to_manifest, deps)), '\n', bf_name) + [
            make_cp_file(bf_name, cp_name)
        ]
        jcmd += ['-classpath', '@' + cp_name]

    if out_dir:
        flags['d'] = out_dir

    if custom_flags:
        fixed_flags = {}
        for k, v in custom_flags.items():
            fixed_flags[resolve_compiler_resource(k)] = resolve_compiler_resource(v)
        flags.update(fixed_flags)

    for k, v in six.iteritems(flags):
        jcmd.append('-{}'.format(k))

        if v:
            jcmd.append(v)

    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
        '--sources-list',
        source_files,
    ]

    if verbose:
        cmd.append('--verbose')

    if nowarn:
        cmd.append('--remove-notes')

    cmd.extend(jcmd + [resolve_compiler_resource(i) for i in custom_opts or []])

    cmd_deps = [
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
    ]

    return cmds + [graph_commands.Cmd(cmd, cwd, cmd_deps)]


def groovyc(source_files, jdk_resource, groovy_compiler_resource, deps=None, out_dir=None, custom_flags=None, cwd=None):
    import jbuild.gen.actions.compile as jgac

    cmds = []
    jcmd = [BuildTools.groovy_compiler_tool(groovy_compiler_resource), '--jointCompilation']

    jcmd += ['@' + source_files]

    flags = {}
    if deps:
        bf_name = '$(BUILD_ROOT)/groovy_bfg.txt'
        manifest_name = '$(BUILD_ROOT)/groovy_bfg.jar'
        cmds += jgac.make_build_file(list(map(jgac.prepare_path_to_manifest, deps)), '\n', bf_name) + [
            make_manifest_from_buildfile(bf_name, manifest_name)
        ]
        jcmd += ['-classpath', manifest_name]

    if out_dir:
        flags['d'] = out_dir

    if custom_flags:
        flags.update(custom_flags)

    for k, v in six.iteritems(flags):
        jcmd.append('-{}'.format(k))

        if v:
            jcmd.append(v)

    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'wrap_groovyc.py'),
        jdk_resource,
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
        '--sources-list',
        source_files,
    ]

    cmd.extend(jcmd)

    cmd_deps = [
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'wrap_groovyc.py'),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'with_pathsep_resolve.py'),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'run_javac.py'),
    ]

    return cmds + [graph_commands.Cmd(cmd, cwd, cmd_deps)]


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


def repack_manifest(target, archive_path, jdk_resource, manifest, cwd, verbose=True):
    gen_mf = gen_vcs_info_cmds(target, archive_path, manifest=manifest, cwd=cwd)
    if not gen_mf:
        return []

    # Temporary directory
    mk_dir = mkdir(os.path.join('_empty', 'META-INF'), cwd=cwd)
    temp_dir = os.path.join(cwd, '_empty')

    touch_manifest = touch(os.path.join('META-INF', 'MANIFEST.MF'), cwd=temp_dir)
    remove_manifest = jar(
        ['META-INF/MANIFEST.MF'], archive_path, jdk_resource, verbose=verbose, cwd=temp_dir, update=True
    )
    update_manifest = jar([], archive_path, jdk_resource, manifest=manifest, verbose=verbose, cwd=temp_dir, update=True)
    return [mk_dir] + gen_mf + [touch_manifest, remove_manifest, update_manifest]


def tar(archive_path, tail, cwd=None):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'find_and_tar.py'),
        archive_path,
        tail,
    ]

    return graph_commands.Cmd(cmd, cwd, [])


def tar_all(tar_name, path, cwd=None):
    script = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'tar_directory.py')
    cmd = [BuildTools.python(), script, tar_name, path, path]

    return graph_commands.Cmd(cmd, cwd, [script])


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


def make_codenav_entry(kythe_to_proto_tool, kindexes, out_name, binding_only, jdk_resource, cwd=None):
    script_path = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'gen_java_codenav_entry.py')
    cmd = (
        [
            BuildTools.python(),
            script_path,
            '--java',
            BuildTools.jdk_tool('java', jdk_path=jdk_resource),
            '--kythe',
            BuildTools.kythe_tool(),
            '--kythe-to-proto',
            kythe_to_proto_tool if kythe_to_proto_tool else BuildTools.kythe_to_proto_tool(),
            '--out-name',
            out_name,
        ]
        + (['--binding-only'] if binding_only else [])
        + kindexes
    )
    return graph_commands.Cmd(cmd, cwd, [script_path])


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


def make_uberjar_cmds(
    inputs,
    out,
    jdk_resource,
    uberjar_resource,
    shade_prefix=None,
    shade_exclude=None,
    path_exclude=None,
    manifest_main=None,
    manifest_attributes=None,
    append_transformers=None,
    service_transformer=False,
    cwd=None,
):
    temp_name = os.path.join(os.path.dirname(out), 'uber.' + os.path.basename(out))
    cmd = [
        BuildTools.jdk_tool('java', jdk_path=jdk_resource),
        '-cp',
        BuildTools.uberjar_tool(uberjar_resource),
        'ru.yandex.devtools.emigrante.Main',
        '--out-jar',
        temp_name,
    ]
    for inp in inputs:
        cmd += ['--jar', inp]
    if shade_prefix:
        cmd += ['--shade-prefix', shade_prefix]
    for exc in shade_exclude or []:
        cmd += ['--shade-exclude', exc]
    for exc in path_exclude or []:
        cmd += ['--uber-exclude', exc]
    if manifest_main:
        cmd += ['--manifest-main', manifest_main]
    for mattr in manifest_attributes or []:
        cmd += ['--manifest-attribute', (':'.join([i.strip() for i in mattr.split(':', 1)]))]
    for atrans in append_transformers or []:
        cmd += ['--append-transformer', atrans]
    if service_transformer:
        cmd += ['--service-transformer']
    return [graph_commands.Cmd(cmd, cwd, []), rm(out, cwd), mv(temp_name, out, cwd)]


def run_gen_script(output, template, properties, jdk_resource, cwd=None):
    cmd = [
        BuildTools.scriptgen_tool(),
        '--output',
        output,
    ]
    if template:
        cmd += ['--template', template]
    if properties:
        cmd += [
            '--properties',
            six.ensure_str(
                base64.b64encode(six.ensure_binary(json.dumps(properties, encoding='utf-8', sort_keys=True))).strip()
            ),
        ]
    cmd += ['--java', BuildTools.jdk_tool('java', jdk_path=jdk_resource)]

    return graph_commands.Cmd(cmd, cwd, ([template] if template else []))


def dump_classpaths(filename, target_jar, pierced_cp):
    jars = []
    for classpath in [target_jar] + pierced_cp:
        jars.append(strip_build_root(classpath))
    return append(filename, "\n".join(jars))


def strip_source_root_or_drop(paths):
    return [strip_source_root(p) for p in paths if p.startswith(consts.SOURCE_ROOT + '/')]


def strip_source_root(path):
    assert path.startswith(consts.SOURCE_ROOT + '/'), path
    return path[len(consts.SOURCE_ROOT) + 1 :]


def strip_build_root(path):
    assert path.startswith(consts.BUILD_ROOT + '/'), path
    return path[len(consts.BUILD_ROOT) + 1 :]


def dump_classpath_source_files(filename, srcsfiles):
    cmd = [
        BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'java_pack_to_file.py'),
        '--output',
        filename,
        '--source-root',
        consts.SOURCE_ROOT,
    ] + [strip_source_root(f) for f in srcsfiles]
    return graph_commands.Cmd(
        cmd,
        None,
        [
            os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'java_pack_to_file.py'),
        ],
    )


def gen_vcs_info_cmds(target, input, manifest, cwd=None):
    if not target.plain.get('EMBED_VCS', None):
        return []

    script = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'vcs_info.py')
    cmd = [BuildTools.python(), script, 'output-java', '$(VCS)/vcs.json', manifest, input]
    return [graph_commands.Cmd(cmd, cwd, [script])]


def gen_jar_filter_cmds(target, jarfile, cwd=None):
    if not target.plain.get(consts.JAR_EXCLUDE_FILTER, None) and not target.plain.get(consts.JAR_INCLUDE_FILTER, None):
        return []

    script = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'filter_zip.py')
    cmd = [
        BuildTools.python(),
        script,
        '--file',
        jarfile,
    ]
    if target.plain.get(consts.JAR_EXCLUDE_FILTER, None):
        for f in sum(target.plain.get(consts.JAR_EXCLUDE_FILTER, []), []):
            cmd += ['--negative', f]
    else:
        for f in sum(target.plain.get(consts.JAR_INCLUDE_FILTER, []), []):
            cmd += ['--positive', f]

    return [graph_commands.Cmd(cmd, cwd, [script])]


def make_cp_file(src, dst, cwd=None):
    if not cwd:
        cwd = consts.BUILD_ROOT
    script_path = os.path.join(consts.SOURCE_ROOT, 'build', 'scripts', 'make_java_classpath_file.py')
    cmd = [BuildTools.python(), script_path, src, dst]
    return graph_commands.Cmd(cmd, cwd, [script_path])
