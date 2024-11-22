from __future__ import print_function
import os
import collections
import exts.yjson as json
import base64
import six

import jbuild.gen.base as base
import jbuild.gen.consts as consts
import jbuild.gen.node as node
import jbuild.commands as commands
import exts.path2 as path2
from . import compile as cmpl
import yalibrary.graph.base as graph_base
import yalibrary.graph.commands as graph_commands

SCRIPT_PATH = os.path.join('build', 'scripts', 'generate_pom.py')
CONTRIB_JAVA = os.path.join('contrib', 'java')

MODULES_FILE = '$(BUILD_ROOT)/modules_list.txt'


class MavenExportException(Exception):
    mute = True


def wrap_execution(cmd):
    return [
        commands.BuildTools.python(),
        '-c',
        'import sys;'
        'import subprocess as sp;'
        'p = sp.Popen({}, stdout=sp.PIPE, stderr=sp.PIPE);'
        'out, err = p.communicate();'
        'rc = p.wait();'
        'sys.stderr.write(\'STDOUT: \' + str(out) + \'\\n\');'
        'sys.stderr.write(\'STDERR: \' + str(err) + \'\\n\');'
        'sys.stderr.write(\'RC: \' + str(rc) + \'\\n\');'
        'sys.exit(rc);'.format(cmd),
    ]


def from_contrib(path):
    return path2.path_startswith(path, CONTRIB_JAVA)


def get_artifact_id(jar_path, ctx):
    dn = graph_base.hacked_normpath(os.path.dirname(jar_path))
    assert dn in ctx.by_path
    target = ctx.by_path[dn]
    assert target.provides_jar()
    candidate_name = target.output_jar_name()[:-4]
    return consts.MAVEN_DEFAULT_GROUP, candidate_name


def get_artifact_coords(jar_path, ctx):
    if from_contrib(jar_path):
        dn = os.path.dirname(jar_path)
        parts = path2.path_explode(dn)

        g = '.'.join(parts[2:-2])
        a = parts[-2]
        v = parts[-1]
        c = ''

        pom_path = os.path.join(ctx.arc_root, os.path.dirname(jar_path), 'pom.xml')
        if os.path.exists(pom_path):
            import xml.etree.ElementTree as et

            with open(pom_path) as f:
                root = et.fromstring(f.read())
            for xpath in ('./{http://maven.apache.org/POM/4.0.0}artifactId', './artifactId'):
                artifact = root.find(xpath)
                if artifact is not None:
                    artifact = artifact.text
                    if a != artifact and a.startswith(artifact):
                        c = a[len(artifact) :].lstrip('-_')
                        a = artifact
                    break

        return g, a, v, c

    else:
        assert ctx.maven_export_version is not None
        g, a = get_artifact_id(jar_path, ctx)
        return g, a, ctx.maven_export_version, ''


def iter_srcs(path, ctx):  # TODO: better
    def _get_srcs_relative_path(src):
        if os.path.exists(os.path.join(ctx.arc_root, path, src)):
            return src or ''
        if os.path.exists(os.path.join(ctx.arc_root, src)):
            prefix = []
            path_copy = graph_base.hacked_normpath(path)
            while path_copy or not src.startswith(path_copy):
                path_copy = '/'.join(path_copy.split('/')[:-1])
                prefix.append('..')
            return graph_base.hacked_path_join(*(prefix + [src]))
        # maybe generated?
        return src or ''

    for src_dir in ctx.by_path[path].plain.get(consts.JAVA_SRCS, []):
        _, src_dir, _, _, _, _ = cmpl.parse_words(src_dir)
        yield _get_srcs_relative_path(src_dir or '.')


def path_test_data(path, ctx):
    test_data = []
    if path in ctx.by_path and consts.T_DATA in ctx.by_path[path].plain and ctx.by_path[path].plain[consts.T_DATA]:
        import itertools

        test_data = list(itertools.chain(*ctx.by_path[path].plain[consts.T_DATA]))
    return test_data


def iter_export_nodes(path, ctx):
    assert path in ctx.by_path
    target = ctx.by_path[path]
    assert target.provides_jar()

    cls_jar = target.output_jar_path()
    src_jar = target.output_sources_jar_path() if target.provides_sources_jar() else None

    g, a, v, _ = get_artifact_coords(base.relativize(cls_jar), ctx)

    pom_relative_path = graph_base.hacked_path_join(path, 'pom.xml')
    pom_file_path = graph_base.hacked_path_join(consts.BUILD_ROOT, pom_relative_path)
    pom_exists = os.path.exists(os.path.join(ctx.arc_root, pom_relative_path)) and not os.path.islink(
        os.path.join(ctx.arc_root, pom_relative_path)
    )

    script = [
        commands.BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, SCRIPT_PATH),
        '--target-path',
        path,
        '--target',
        ':'.join([g, a, v]),
        '--pom-path',
        pom_file_path,
    ]

    if not from_contrib(path):
        ctx.maven_export_modules_list.add(path)

    ins = [(cls_jar, node.FILE)]
    if ctx.opts.dump_sources and src_jar:
        ins.append((src_jar, node.FILE))

    cmds = [graph_commands.Cmd(script, cwd=None, inputs=[])]

    if (not from_contrib(path) or not pom_exists) and ctx.opts.deploy:
        if not ctx.opts.repository_id:
            raise MavenExportException('Maven repository id is not set')

        if not ctx.opts.repository_url:
            raise MavenExportException('Maven repository url is not set')

        mvn_deploy = [
            os.path.join('$(MAVEN)', 'bin', 'mvn'),
            'deploy:deploy-file',
            '-DpomFile={}'.format(pom_file_path),
            '-Dfile={}'.format(cls_jar),
            '-DrepositoryId={}'.format(ctx.opts.repository_id),
            '-Durl={}'.format(ctx.opts.repository_url),
            '-Djava.net.preferIPv4Addresses=false',
            '-Djava.net.preferIPv6Addresses=true',
        ]

        if ctx.opts.dump_sources and src_jar:
            mvn_deploy.append('-Dsources={}'.format(src_jar))

        if ctx.opts.maven_settings:
            mvn_deploy.append('-gs')
            mvn_deploy.append(ctx.opts.maven_settings)
            mvn_deploy.append('-s')
            mvn_deploy.append(ctx.opts.maven_settings)

        if ctx.opts.be_verbose:
            mvn_deploy.append('-e')
            mvn_deploy.append('-X')
            cmds.append(graph_commands.Cmd(wrap_execution(mvn_deploy), cwd=None, inputs=[]))
        else:
            cmds.append(graph_commands.Cmd(mvn_deploy, cwd=None, inputs=[]))

    ext_paths = []
    for dep in ctx.classpath(path):
        dep_target_path = graph_base.hacked_normpath(base.relativize(os.path.dirname(dep)))
        assert dep_target_path in ctx.by_path
        ext_paths.append(dep_target_path)

    if not from_contrib(path) or not pom_exists:
        yield node.JNode(
            path,
            cmds,
            ins,
            node.files([pom_file_path]),
            resources=['MAVEN'],
            res=cls_jar in ctx.maven_export_result(),
            fake_id=target.fake_id(),
        ), list(map(base.relativize, ext_paths))


# entrypoint: generates root pom.xml node
def export_root(ctx):
    pom_path = os.path.join(consts.BUILD_ROOT, 'pom.xml')
    script = [
        commands.BuildTools.python(),
        os.path.join(consts.SOURCE_ROOT, SCRIPT_PATH),
        '--target',
        '%s:root-for-%s:%s'
        % (
            'ru.yandex',
            '-'.join([graph_base.hacked_normpath(x).replace('/', '-').strip('-') for x in ctx.opts.rel_targets]),
            ctx.maven_export_version,
        ),
        '--pom-path',
        pom_path,
        '--properties',
        six.ensure_str(
            base64.b64encode(six.ensure_binary(json.dumps({'arcadia.root': ctx.arc_root}, ensure_ascii=False)))
        ),
        '--packaging',
        'pom',
        '--modules-path',
        MODULES_FILE,
    ]
    return node.JNode(
        '',
        [commands.rm(MODULES_FILE)]
        + cmpl.make_build_file(ctx.maven_export_modules_list, '\n', MODULES_FILE)
        + [graph_commands.Cmd(script, cwd=None, inputs=[])],
        [],
        node.files([pom_path]),
        resources=['MAVEN'],
    )


def export_to_maven(path, _, ctx):
    if not ctx.by_path[path].provides_jar():
        return

    export_queue = collections.deque()
    export_queue.append(path)
    seen = ctx.external_maven_import

    while len(export_queue):
        cur = export_queue.popleft()

        if cur in seen:
            continue

        seen.add(cur)

        for export_node, ext_paths in iter_export_nodes(cur, ctx):
            for p in ext_paths:
                if p not in seen:
                    export_queue.append(p)

            yield export_node
