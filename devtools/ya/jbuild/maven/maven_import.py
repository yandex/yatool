import six

import os
import exts.yjson as json
import uuid
import copy
import logging
import tempfile
import base64
import re
import jinja2

import library.python.filelock
import library.python.resource as rs

import exts.fs as fs
import exts.tmp as tmp
import exts.hashing as hashing
import yalibrary.makelists as ml
import yalibrary.makelists.macro_definitions as md

import devtools.ya.build.build_opts as bo
import devtools.ya.build.build_handler
import devtools.ya.core.yarg
import devtools.ya.core.config

import devtools.ya.jbuild.gen.gen as gen
import devtools.ya.jbuild.gen.base as base
import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.node as jnode
import devtools.ya.jbuild.execute as execute
import devtools.ya.jbuild.commands as commands
import devtools.ya.jbuild.maven.license as license
import devtools.ya.jbuild.maven.version_filter as vf

import yalibrary.graph.base as graph_base

from devtools.ya.handlers.dump import FullForcedDepsOptions, do_forced_deps

logger = logging.getLogger(__name__)

IMPORTER_PATH = graph_base.hacked_path_join('devtools', 'maven-import')
IMPORTER_MAIN = 'ru.yandex.devtools.maven.importer.Importer'
DEFAULT_OWNER = 'g:java-contrib'
CONTRIB_REL_PATH = 'contrib/java'


def get_contrib_path():
    return os.environ.get('YA_JBUILD_CONTRIB_PATH', CONTRIB_REL_PATH)


class ImportError(Exception):
    mute = True


class ArtifactParseError(Exception):
    mute = True


class MavenImporterError(Exception):
    mute = True


class ArtifactUploadError(Exception):
    mute = True


class NoArtefactVersionError(Exception):
    mute = True


class Artifact(object):
    FORMAT = '<groupId>:<artifactId>[:<extension>[:<classifier>]]:<version>'

    def __init__(self, group_id, artifact_id, version, extension, classifier):
        self.group_id = group_id
        self.artifactId = artifact_id
        self.version = version
        self.extension = extension
        self.classifier = classifier

    def coordinates(self):
        return (self.group_id, self.artifactId, self.extension, self.classifier, self.version)

    def __hash__(self):
        return hash('#'.join(self.coordinates()))

    @staticmethod
    def from_dict(dct):
        return Artifact(
            dct['groupId'],
            dct['artifactId'],
            dct['version'],
            dct.get('extension'),
            dct.get('classifier'),
        )

    @staticmethod
    def from_unified_dict(dct, set_version=True):
        return Artifact(
            dct['group_id'],
            dct['artifact_id'],
            dct.get('version') if set_version else None,
            dct.get('extension'),
            dct.get('classifier'),
        )

    @staticmethod
    def from_string(s):
        if s.startswith(CONTRIB_REL_PATH):
            parts = os.path.normpath(s[len(CONTRIB_REL_PATH) + 1 :]).split(os.path.sep)
            tokens = ['.'.join(parts[:-2]), parts[-2], parts[-1]]
        else:
            tokens = s.split(':')

        if len(tokens) < 3 or len(tokens) > 5:
            raise ArtifactParseError('Expected: {}, but got: {}'.format(Artifact.FORMAT, s))

        group_id = tokens[0]
        artifact_id = tokens[1]
        version = tokens[-1]
        extension = None
        classifier = None

        if len(tokens) > 3:
            extension = tokens[2]

        if len(tokens) > 4:
            classifier = tokens[3]

        if not group_id or not artifact_id or not version:
            raise ArtifactParseError('Expected: {}, but got: {}'.format(Artifact.FORMAT, s))

        return Artifact(group_id, artifact_id, version, extension, classifier)

    def __str__(self):
        return ':'.join(filter(bool, self.coordinates()))


def contrib_location(artifact):
    path = [get_contrib_path()] + artifact.group_id.split('.')

    if not artifact.classifier or artifact.classifier == 'sources':
        suffix = ''
    else:
        suffix = '-' + artifact.classifier
    path.append(artifact.artifactId + suffix)

    if artifact.version:
        path.append(artifact.version)

    return graph_base.hacked_path_join(*path)


def contrib_location_bom(artifact):
    return graph_base.hacked_path_join(
        *(
            [get_contrib_path()]
            + artifact.group_id.split('.')
            + [artifact.artifactId, artifact.version, 'ya.dependency_management.inc']
        )
    )


class Node(object):
    def __init__(self, artifact, path=None, resource_id=None, deps=None, licenses=None, repository=None):
        self.artifact = artifact
        self.path = path
        self.resource_id = resource_id
        self.deps = deps or []
        self.licenses = licenses or []
        self.repository = repository


def provides_jwar(node):
    return node.path and node.path[-4:] in ('.jar', '.war', '.aar')


def restore_graph(meta):
    node_by_coords = {}
    order = []

    def get_or_create_node(artifact):
        if artifact.coordinates() not in node_by_coords:
            node_by_coords[artifact.coordinates()] = Node(artifact)

        return node_by_coords[artifact.coordinates()]

    for meta_node in meta:
        artifact = Artifact.from_dict(meta_node['artifact'])
        path = meta_node.get('path')
        deps = [get_or_create_node(Artifact.from_dict(x)) for x in meta_node.get('dependencies', [])]

        node = get_or_create_node(artifact)
        licenses = meta_node.get('licenses', [])
        repository = meta_node.get('repository')

        if path:
            if node.path:
                assert path == node.path

            else:
                node.path = path

        if deps:
            if node.deps:
                deps_coords = set([n.artifact.coordinates() for n in deps])
                node_deps_coords = set([n.artifact.coordinates() for n in node.deps])

                assert deps_coords == node_deps_coords

            else:
                node.deps = deps

        if licenses:
            for lic in licenses:
                if lic not in node.licenses:
                    node.licenses.append(lic)
        if repository:
            node.repository = repository

        order.append(artifact.coordinates())

    return [node_by_coords[coordinates] for coordinates in graph_base.uniq_first_case(order)]


def upload_artifacts_parallel_and_cacheble(nodes, opts, app_ctx):
    graph = []
    resource_ids = {}

    def collect_id_func(node, result_path):
        return lambda rmap: resource_ids.update({node.artifact: open(rmap.fix(result_path)).read()})

    for node in nodes:
        result_path = os.path.join(consts.BUILD_ROOT, 'resource_id')

        # Create cachable node to upload jar
        real_upload_node = jnode.JNode(
            '', [], [(str(node.artifact), jnode.FILE)], [(result_path, jnode.FILE)]
        ).to_serializable()
        real_upload_node['uid'] = str(hashing.fast_filehash(node.path))
        real_upload_node['kv']['pc'] = 'yellow'
        real_upload_node['kv']['p'] = 'UPLOAD'

        if opts.dry_run:
            real_upload_node['func'] = lambda rmap: open(rmap.fix(result_path), 'w').write('0')
            real_upload_node['cache'] = False

        else:
            cmd = commands.upload_resource(
                os.path.basename(node.path),
                result_path,
                resource_type='JAVA_LIBRARY',
                owner=opts.resource_owner,
                token=opts.oauth_token,
                transport=opts.transport,
                cwd=os.path.dirname(node.path),
            )

            real_upload_node['cmds'].append({'cmd_args': cmd.cmd, 'cwd': cmd.cwd})

        # Create uncachable node to get uploaded resource id from cache
        collecting_id_node = jnode.JNode('', [], [(result_path, jnode.FILE)], []).to_serializable()
        collecting_id_node['uid'] = str(uuid.uuid4())
        collecting_id_node['kv']['pc'] = 'grey'
        collecting_id_node['kv']['p'] = 'FINISHING'
        collecting_id_node['cache'] = False
        collecting_id_node['deps'] = [real_upload_node['uid']]
        collecting_id_node['func'] = collect_id_func(node, result_path)

        graph.extend([real_upload_node, collecting_id_node])

    # Collapse nodes by uid because the same jar may have different coordinates in different repos
    by_uid = {n['uid']: n for n in graph}
    result = by_uid.keys()
    graph = by_uid.values()

    graph_to_log = []
    for n in graph:
        if 'func' in n:
            n = n.copy()
            n['func'] = str(n['func'])
        graph_to_log.append(n)
    logger.debug('Upload graph: %s', json.dumps(graph_to_log, default=str, indent=4, sort_keys=True))

    pattern2tool = execute.DEFAULT_JAVA_PATTERN_TOOL_MAP
    pattern2tool['MAVEN_ARTIFACT_UPLOADER'] = 'maven_import_sandbox_uploader'

    res, rc = execute.execute(
        graph,
        result,
        complete_jbuild_opts(opts),
        app_ctx,
    )

    if rc != 0:
        raise ArtifactUploadError('Can\'t upload one or more artifacts.')

    for node in nodes:
        node.resource_id = resource_ids[node.artifact]


def upload_artifacts_parallel_and_cacheble_unified(poms, opts, app_ctx):
    def get_nodes(fname):
        result_path = os.path.join(consts.BUILD_ROOT, os.path.splitext(os.path.basename(fname))[0] + '_resource_id')

        # Create cachable node to upload jar
        real_upload_node = jnode.JNode('', [], [(fname, jnode.FILE)], [(result_path, jnode.FILE)]).to_serializable()
        real_upload_node['uid'] = six.ensure_str(
            base64.b64encode(six.ensure_binary(str(hashing.fast_filehash(fname)) + str(os.path.basename(fname))))
        )
        real_upload_node['kv']['pc'] = 'yellow'
        real_upload_node['kv']['p'] = 'UPLOAD'

        if opts.dry_run:
            real_upload_node['func'] = lambda rmap: open(rmap.fix(result_path), 'w').write('0')
            real_upload_node['cache'] = False

        else:
            cmd = commands.upload_resource(
                os.path.basename(fname),
                result_path,
                resource_type='JAVA_LIBRARY',
                owner=opts.resource_owner,
                token=opts.oauth_token,
                transport=opts.transport,
                cwd=os.path.dirname(fname),
            )

            real_upload_node['cmds'].append({'cmd_args': cmd.cmd, 'cwd': cmd.cwd})

        # Create uncachable node to get uploaded resource id from cache
        collecting_id_node = jnode.JNode('', [], [(result_path, jnode.FILE)], []).to_serializable()
        collecting_id_node['uid'] = str(uuid.uuid4())
        collecting_id_node['kv']['pc'] = 'grey'
        collecting_id_node['kv']['p'] = 'FINISHING'
        collecting_id_node['cache'] = False
        collecting_id_node['deps'] = [real_upload_node['uid']]
        collecting_id_node['func'] = collect_id_func(fname, result_path)
        return [real_upload_node, collecting_id_node]

    graph = []
    resource_ids = {}

    def collect_id_func(fname, result_path):
        return lambda rmap: resource_ids.update({fname: open(rmap.fix(result_path)).read()})

    for pom in poms:
        if pom.get('jar_file'):
            graph.extend(get_nodes(pom['jar_file']))
        if pom.get('source_file'):
            graph.extend(get_nodes(pom['source_file']))

    # Collapse nodes by uid because the same jar may have different coordinates in different repos
    by_uid = {n['uid']: n for n in graph}
    result = by_uid.keys()
    graph = by_uid.values()

    graph_to_log = []
    for n in graph:
        if 'func' in n:
            n = n.copy()
            n['func'] = str(n['func'])
        graph_to_log.append(n)
    logger.debug('Upload graph: %s', json.dumps(graph_to_log, default=str, indent=4, sort_keys=True))

    pattern2tool = execute.DEFAULT_JAVA_PATTERN_TOOL_MAP
    pattern2tool['MAVEN_ARTIFACT_UPLOADER'] = 'maven_import_sandbox_uploader'

    res, rc = execute.execute(
        graph,
        result,
        complete_jbuild_opts(opts),
        app_ctx,
    )

    if rc != 0:
        raise ArtifactUploadError('Can\'t upload one or more artifacts.')

    for pom in poms:
        if pom.get('jar_file'):
            pom['jar_file_id'] = resource_ids[pom['jar_file']]
        if pom.get('source_file'):
            pom['source_file_id'] = resource_ids[pom['source_file']]


def find_jdk_pattern(resources):
    for resource in resources:
        if re.match('^JDK\\d+-', resource['pattern']) or resource['pattern'].startswith(
            'JDK-'
        ):  # remove "or" after DEVTOOLS-8269 is done
            return '$({})'.format(resource['pattern'])
    return '${}'.format(consts.JDK_DEFAULT_RESOURCE)


def resolve_transitively(artifacts, local_repo, remote_repos, opts, app_ctx, resolve_type):
    temp_project = os.path.join(devtools.ya.core.config.user_junk_dir(), 'maven_import_tmp')
    temp_project_path = os.path.join(opts.arc_root, temp_project)
    session_id = str(uuid.uuid4())

    cache = os.path.join(devtools.ya.core.config.misc_root(), 'maven_import')
    locks = fs.create_dirs(os.path.join(cache, 'locks'))
    lock_path = os.path.join(locks, '{}.lock'.format(hashing.md5_value(temp_project_path)))
    with library.python.filelock.FileLock(lock_path):
        if os.path.exists(temp_project_path):
            fs.remove_tree_safe(temp_project_path)
        fs.ensure_dir(temp_project_path)
        with open(os.path.join(temp_project_path, 'ya.make'), 'w') as f:
            jinja_resource = six.ensure_str(rs.find('maven_import_tmp/ya.make.jinja'))
            f.write(
                jinja2.Template(jinja_resource).render(
                    artifacts=artifacts,
                    local_repo_abs_path=os.path.abspath(local_repo),
                    remote_repos=remote_repos,
                    resolve_type=resolve_type,
                    session_id=session_id,
                    minimal_pom_validation=opts.minimal_pom_validation,
                    skip_artifacts=opts.skip_artifacts,
                    import_dm=opts.import_dm,
                    ignore_errors=opts.ignore_errors,
                    repo_auth_username=opts.repo_auth_username,
                    repo_auth_password=opts.repo_auth_password,
                )
            )
    merge_opts = devtools.ya.core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
    merge_opts.create_symlinks = False
    merge_opts.output_root = tempfile.mkdtemp()
    build_opts = merge_opts.initialize([])
    build_opts.__dict__.update(opts.__dict__)
    build_opts.abs_targets = [os.path.join(opts.arc_root, temp_project)]
    rc = devtools.ya.build.build_handler.do_ya_make(build_opts)
    fs.remove_tree_safe(temp_project_path)

    if rc != 0:
        raise MavenImporterError("Can't resolve one or more artifacts.")

    meta_files_dir = os.path.join(build_opts.output_root, temp_project, session_id)
    if resolve_type != 'unified':
        assert len(os.listdir(meta_files_dir)) == len(artifacts)
    if resolve_type in ('bom', 'check'):
        meta = {}
        for fname in os.listdir(meta_files_dir):
            meta.update(json.load(open(os.path.join(meta_files_dir, fname))))

    elif resolve_type == 'unified':
        meta = {}
        for fname in os.listdir(meta_files_dir):
            raw_meta = json.load(open(os.path.join(meta_files_dir, fname)))
            keys = meta.keys() | raw_meta.keys()
            meta = {key: meta.get(key, []) + raw_meta.get(key, []) for key in keys}

    else:
        meta = []
        # Collect artifacts dependencies meta information
        for fname in os.listdir(meta_files_dir):
            meta.extend(json.load(open(os.path.join(meta_files_dir, fname))))

    logger.debug('Meta: %s', json.dumps(meta, indent=4, sort_keys=True))

    return restore_graph(meta) if resolve_type == 'jar' else meta


def complete_jbuild_opts(opts):
    o = gen.default_opts()
    o.__dict__.update(opts.__dict__)  # default opts are not intercepted
    o.create_symlinks = False
    o.output_root = tempfile.mkdtemp()
    return o


def find_or_create(project, name):
    try:
        return project.find_siblings(name)[0]

    except IndexError:
        macro = name() if callable(name) else md.Macro(name)
        project.append_child(macro)

        return macro


def attach_sources_nodes(nodes):
    by_coords = base.group_by(
        nodes,
        lambda node: (
            node.artifact.group_id,
            node.artifact.artifactId,
            node.artifact.extension,
            node.artifact.version,
        ),
    )

    extra_sources_nodes = []

    for coords, ns in six.iteritems(by_coords):
        sources = [n for n in ns if n.artifact.classifier == 'sources']  # len(sources) <= 1
        others = [n for n in ns if n.artifact.classifier != 'sources']

        for other in others:
            for source in sources:
                # Attach source to other
                extra = copy.deepcopy(source)

                if other.artifact.classifier:
                    extra.artifact.artifactId += '-' + other.artifact.classifier

                extra_sources_nodes.append(extra)

    return [n for n in nodes if n.artifact.classifier != 'sources'] + extra_sources_nodes


def populate_contrib(
    arcadia, nodes, owner=None, dry_run=False, write_licenses=False, forced_deps=None, local_jar_resources=False
):
    forced_deps = forced_deps_as_list(forced_deps)
    reachable = {
        get_contrib_path(),
    }
    owner = owner or DEFAULT_OWNER
    poms = {}
    jars = {}

    module_type = 'JAVA_CONTRIB'
    if any(node.artifact.extension == 'aar' for node in nodes):
        module_type = 'AAR_CONTRIB'
    elif any(node.artifact.extension == 'war' for node in nodes):
        module_type = 'WAR_CONTRIB'
    project_by_path = dict((contrib_location(n.artifact), md.Project(module_type)) for n in nodes)

    # Create arcadia projects, don't write anything
    for node in nodes:
        project_path = contrib_location(node.artifact)
        project = project_by_path[project_path]

        if arcadia.exists(project_path):
            continue

        # OWNER
        macro = find_or_create(project, 'SUBSCRIBER')

        if not macro.find_nodes(owner):
            macro.add_value(owner)

        # LICENSE
        if write_licenses and node.licenses:
            macro_license = find_or_create(project, md.LicenseMacro)
            macro_license.children = []
            for lic in node.licenses:
                macro_license.add_value(lic)

        # PEERDIR
        peerdirs = [contrib_location(n.artifact) for n in node.deps]

        if peerdirs:
            macro = find_or_create(project, 'PEERDIR')

            for peerdir in peerdirs:
                macro.add_value(get_peerdir(peerdir, forced_deps))

        # JAR_RESOURCE/SRC_RESOURCE/AAR_RESOURCE/WAR_RESOURCE
        if node.resource_id:
            if node.artifact.classifier == 'sources':
                macro = (
                    find_or_create(project, 'LOCAL_SOURCES_JAR')
                    if local_jar_resources
                    else find_or_create(project, 'SRC_RESOURCE')
                )
                macro.children = []
                macro.children.append(md.Value(str(node.resource_id)))
                if local_jar_resources:
                    jars[os.path.join(arcadia.arc_root, project_path, node.resource_id)] = node.path

            elif node.artifact.extension == 'war':
                macro = find_or_create(project, 'WAR_RESOURCE')
                macro.children = []
                macro.children.append(md.Value(str(node.resource_id)))

            elif node.artifact.extension == 'aar':
                macro = find_or_create(project, 'AAR_RESOURCE')
                macro.children = []
                macro.children.append(md.Value(str(node.resource_id)))

            else:
                macro = (
                    find_or_create(project, 'LOCAL_JAR')
                    if local_jar_resources
                    else find_or_create(project, 'JAR_RESOURCE')
                )
                macro.children = []
                macro.children.append(md.Value(str(node.resource_id)))
                if local_jar_resources:
                    jars[os.path.join(arcadia.arc_root, project_path, node.resource_id)] = node.path

        # Check pom in local repo
        for fname in os.listdir(os.path.dirname(node.path)):
            if fname.endswith('pom'):
                poms[project_path] = os.path.join(os.path.dirname(node.path), fname)

    for project in six.itervalues(project_by_path):
        # END
        find_or_create(project, 'END')

    # Populate contrib
    for project_path, project in six.iteritems(project_by_path):
        if arcadia.exists(project_path):
            continue

        # Write makelist
        if not dry_run:
            fs.create_dirs(os.path.join(arcadia.arc_root, project_path))
            project.write(os.path.join(arcadia.arc_root, project_path, ml.MAKELIST_NAME))

        else:
            continue

        # Create peerdir from artifact to max artifact version
        parent_path = os.path.dirname(project_path)

        contrib_versions = [
            v
            for v in os.listdir(os.path.join(arcadia.arc_root, parent_path))
            if arcadia.exists(os.path.join(parent_path, v))
        ]
        max_version = max(contrib_versions, key=lambda v: vf.listify_version(v))
        max_peerdir = get_peerdir(os.path.join(parent_path, max_version), forced_deps)

        if arcadia.exists(parent_path):
            parent_project = arcadia[parent_path].makelist()

            macro = find_or_create(parent_project, 'PEERDIR')
            macro.children = []
            macro.add_value(max_peerdir)

        else:
            parent_project = md.Project('JAVA_CONTRIB_PROXY')
            find_or_create(parent_project, 'SUBSCRIBER').add_value(owner)
            find_or_create(parent_project, 'PEERDIR').add_value(max_peerdir)
            find_or_create(parent_project, 'END')

        parent_project.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

        # Ensure it is recurse reachable
        def ensure_reachability(path):
            if path not in reachable:
                parent_path = os.path.dirname(path)

                ensure_reachability(parent_path)

                if arcadia.exists(parent_path):
                    parent_project = arcadia[parent_path].makelist()

                    macro = find_or_create(parent_project, 'RECURSE')

                    if not macro.find_values(os.path.basename(path)):
                        macro.add_value(os.path.basename(path))
                        parent_project.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

                else:
                    recurse_macro = md.Macro('RECURSE')
                    recurse_macro.add_value(os.path.basename(path))
                    owner_macro = md.Macro('SUBSCRIBER')
                    owner_macro.add_value(owner)
                    node = md.Node('', md.TYPE_UNKNOWN)
                    node.children.extend([owner_macro, recurse_macro])
                    node.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

                reachable.add(path)

        ensure_reachability(project_path)

    if dry_run:
        for path, content in (
            (os.path.join(project_path, ml.MAKELIST_NAME), '\n'.join(project._write()))
            for project_path, project in six.iteritems(project_by_path)
        ):
            logger.info(path + '\n' + content)
    else:
        # Copy poms from local repo
        for project_path, pom_path in six.iteritems(poms):
            fs.copy_file(pom_path, os.path.join(arcadia.arc_root, project_path, 'pom.xml'))
        if local_jar_resources:
            # Copy jars from local repo
            for dst_jar, src_jar in six.iteritems(jars):
                fs.copy_file(src_jar, dst_jar)

    # Summarize
    logger.info('Artifact(s)%s added:\n%s', ' would be' if dry_run else '', '\n'.join(path for path in project_by_path))


def populate_contrib_unified(
    arcadia, origin, owner=None, dry_run=False, write_licenses=False, forced_deps=None, local_jar_resources=False
):
    forced_deps = forced_deps_as_list(forced_deps)
    reachable = {
        get_contrib_path(),
    }
    owner = owner or DEFAULT_OWNER
    poms = {}
    jars = {}

    project_by_path = {}

    # Create arcadia projects, don't write anything
    for p in origin:
        project_path = contrib_location(p['artifact'])
        if arcadia.exists(project_path):
            continue
        project = md.Project('JAVA_CONTRIB')
        project_by_path[project_path] = project

        # SUBSCRIBER
        macro = find_or_create(project, 'SUBSCRIBER')

        if not macro.find_nodes(owner):
            macro.add_value(owner)

        # VERSION
        artifact = p.get('artifact')
        version_macro = find_or_create(project, 'VERSION')
        version_macro.add_value(artifact.version)

        # LICENSE
        if write_licenses and p.get('licenses', []):
            license_macro = find_or_create(project, md.LicenseMacro)
            for lic in p.get('licenses'):
                license_macro.add_value(lic)

        # REPOSITORY
        if p.get('repository'):
            repository_macro = find_or_create(project, 'ORIGINAL_SOURCE')
            repository_macro.children = []
            repository_macro.add_value(p.get('repository'))

        # if p['generate_ya_dependency_management_inc']:
        #     macro = md.StringMacro('INCLUDE')
        #     macro.add_value('${ARCADIA_ROOT}/' + contrib_location_bom(p['artifact']))
        #     project.append_child(macro)

        # PEERDIR
        peerdirs = [contrib_location(d) for d in p.get('peerdirs', [])]

        if peerdirs:
            macro = find_or_create(project, 'PEERDIR')

            for peerdir in peerdirs:
                macro.add_value(get_peerdir(peerdir, forced_deps))

        # EXCLUDE
        if p.get('excludes', []):
            macro = find_or_create(project, 'EXCLUDE')
            for exclude in p.get('excludes', []):
                if exclude.group_id == "*" or exclude.artifactId == "*":
                    continue

                macro.add_value(contrib_location(exclude))

        if p.get('jar_file_id'):
            macro = (
                find_or_create(project, 'LOCAL_JAR') if local_jar_resources else find_or_create(project, 'JAR_RESOURCE')
            )
            macro.children = []
            macro.add_value(str(p.get('jar_file_id')))
            if local_jar_resources:
                jars[os.path.join(arcadia.arc_root, project_path, p['jar_file_id'])] = p['jar_file']
            if p.get('source_file_id'):
                macro = (
                    find_or_create(project, 'LOCAL_SOURCES_JAR')
                    if local_jar_resources
                    else find_or_create(project, 'SRC_RESOURCE')
                )
                macro.children = []
                macro.add_value(str(p['source_file_id']))
                if local_jar_resources:
                    jars[os.path.join(arcadia.arc_root, project_path, p['source_file_id'])] = p['source_file']

        if p.get('pom_file'):
            poms[project_path] = p['pom_file']

    for project in six.itervalues(project_by_path):
        # END
        find_or_create(project, 'END')

    # Populate contrib
    for project_path, project in six.iteritems(project_by_path):
        if arcadia.exists(project_path):
            continue

        # Write makelist
        if not dry_run:
            fs.create_dirs(os.path.join(arcadia.arc_root, project_path))
            project.write(os.path.join(arcadia.arc_root, project_path, ml.MAKELIST_NAME))

        else:
            continue

        # Create peerdir from artifact to max artifact version
        parent_path = os.path.dirname(project_path)

        contrib_versions = [
            v
            for v in os.listdir(os.path.join(arcadia.arc_root, parent_path))
            if arcadia.exists(os.path.join(parent_path, v))
        ]
        max_version = max(contrib_versions, key=lambda v: vf.listify_version(v))
        max_peerdir = get_peerdir(graph_base.hacked_path_join(parent_path, max_version), forced_deps)

        if arcadia.exists(parent_path):
            parent_project = arcadia[parent_path].makelist()

            macro = find_or_create(parent_project, 'PEERDIR')
            macro.children = []
            macro.add_value(max_peerdir)
        else:
            parent_project = md.Project('JAVA_CONTRIB_PROXY')
            find_or_create(parent_project, 'SUBSCRIBER').add_value(owner)
            find_or_create(parent_project, 'PEERDIR').add_value(max_peerdir)
            find_or_create(parent_project, 'END')

        parent_project.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

        # Ensure it is recurse reachable
        def ensure_reachability(path):
            if path not in reachable:
                parent_path = os.path.dirname(path)

                ensure_reachability(parent_path)

                if arcadia.exists(parent_path):
                    parent_project = arcadia[parent_path].makelist()

                    macro = find_or_create(parent_project, 'RECURSE')

                    if not macro.find_values(os.path.basename(path)):
                        macro.add_value(os.path.basename(path))
                        parent_project.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

                else:
                    recurse_macro = md.Macro('RECURSE')
                    recurse_macro.add_value(os.path.basename(path))
                    owner_macro = md.Macro('SUBSCRIBER')
                    owner_macro.add_value(owner)
                    node = md.Node('', md.TYPE_UNKNOWN)
                    node.children.extend([owner_macro, recurse_macro])
                    node.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

                reachable.add(path)

        ensure_reachability(project_path)

    if dry_run:
        for path, content in (
            (os.path.join(project_path, ml.MAKELIST_NAME), '\n'.join(project._write()))
            for project_path, project in six.iteritems(project_by_path)
        ):
            logger.info(path + '\n' + content)
    else:
        # Copy poms from local repo
        for project_path, pom_path in six.iteritems(poms):
            fs.copy_file(pom_path, os.path.join(arcadia.arc_root, project_path, 'pom.xml'))
        if local_jar_resources:
            # Copy jars from local repo
            for dst_jar, src_jar in six.iteritems(jars):
                fs.copy_file(src_jar, dst_jar)

    # Summarize
    logger.info('Artifact(s)%s added:\n%s', ' would be' if dry_run else '', '\n'.join(path for path in project_by_path))


def populate_bombs_unified(arcadia, origin, dry_run=False, forced_deps=None):
    forced_deps = forced_deps_as_list(forced_deps)
    project_by_path = {}

    # Create arcadia projects, don't write anything
    for p in origin:
        project_path = contrib_location_bom(p['artifact'])
        if arcadia.exists(project_path):
            continue

        parts = []
        for inc in p['includes']:
            parts.append('INCLUDE(${{ARCADIA_ROOT}}/{})'.format(contrib_location_bom(inc)))

        if p['dm']:
            if parts:
                parts.append('\n')
            parts.append('DEPENDENCY_MANAGEMENT(')
            for dep in p['dm']:
                parts.append(get_dm_line(contrib_location(dep), forced_deps))
            parts.append(')')
        project_by_path[project_path] = parts

    # Populate bombs
    for project_path, parts in six.iteritems(project_by_path):
        if arcadia.exists(project_path):
            continue

        # Write makelist
        if not dry_run:
            fs.create_dirs(os.path.join(arcadia.arc_root, os.path.dirname(project_path)))
            with open(os.path.join(arcadia.arc_root, project_path), 'w') as f:
                f.write('\n'.join(parts) + '\n')
        else:
            continue

    if dry_run:
        for path, content in (
            (project_path, '\n'.join(parts)) for project_path, parts in six.iteritems(project_by_path)
        ):
            logger.info(path + '\n' + content)

    # Summarize
    logger.info('Boms(s)%s added:\n%s', ' would be' if dry_run else '', '\n'.join(path for path in project_by_path))


def populate_bombs(arcadia, meta, dry_run=False, forced_deps=None):
    def get_dep_path(dep):
        splitted = dep.split(':')
        return graph_base.hacked_path_join(get_contrib_path(), *(splitted[0].split('.') + splitted[1:]))

    forced_deps = forced_deps_as_list(forced_deps)
    missing_projects = set()

    populated = set()
    # Populate bombs
    for bom_path, data in six.iteritems(meta):
        for dep in data['dependencies']:
            dep_path = get_dep_path(dep)
            if not arcadia.exists(dep_path):
                missing_projects.add(dep)

        if os.path.exists(os.path.join(arcadia.arc_root, get_contrib_path(), bom_path)):
            continue
        populated.add(graph_base.hacked_normpath(graph_base.hacked_path_join(get_contrib_path(), bom_path)))

        # Write .inc
        if not dry_run and (data['includes'] or data['dependencies']):
            bom_path = os.path.join(arcadia.arc_root, get_contrib_path(), bom_path)
            fs.create_dirs(os.path.dirname(bom_path))
            with open(bom_path, 'w') as f:
                for i in data['includes']:
                    f.write(
                        'INCLUDE(${{ARCADIA_ROOT}}/{})\n'.format(
                            graph_base.hacked_path_join(get_contrib_path(), os.path.normpath(i))
                        )
                    )
                if data['includes'] and data['dependencies']:
                    f.write('\n')
                if data['dependencies']:
                    f.write('DEPENDENCY_MANAGEMENT(\n')
                    for dep in data['dependencies']:
                        dep_path = get_dep_path(dep)
                        f.write(get_dm_line(dep_path, forced_deps))
                    f.write(')\n')
        else:
            continue

    # Summarize
    if missing_projects:
        logger.warning(
            'DEPENDENCY_MANAGEMENT sections contains missing projects:\n{}\n\nYou can add this artifacts manual\n\n'.format(
                '\n'.join(sorted(missing_projects))
            )
        )
    logger.info('Bom(s)%s added:\n%s', ' would be' if dry_run else '', '\n'.join(sorted(populated)))


def do_import(opts):
    forced_deps = get_forced_deps(opts)
    arcadia = ml.Arcadia(opts.arc_root)
    contrib = os.path.join(opts.arc_root, get_contrib_path())
    repos_file = os.path.join(contrib, 'MAVEN_REPOS')
    license_aliases_file = os.path.join(contrib, 'LICENSE_ALIASES')
    trusted_repos_file = os.path.join(contrib, 'TRUSTED_REPOS')
    request_artifacts = list(map(Artifact.from_string, set(opts.libs)))
    if opts.unified_mode:
        logger.info('Unified importer used')
        logger.info('Try to import: ' + ' '.join(set(opts.libs)))
        import_unified(
            opts,
            arcadia,
            request_artifacts,
            repos_file,
            license_aliases_file,
            trusted_repos_file,
            opts.write_licenses,
            opts.canonize_licenses,
            forced_deps,
        )
        logger.info('Done')
    else:
        jars, poms = split_artifacts_by_type(opts, arcadia, request_artifacts, repos_file)
        if poms:
            logger.info('Try to add bom files: ' + ' '.join(poms))
            import_bombs(opts, arcadia, list(map(Artifact.from_string, poms)), repos_file, forced_deps)
            logger.info('Done')
        if jars:
            logger.info('Try to import artifacts: ' + ' '.join(jars))
            import_artifacts(
                opts,
                arcadia,
                list(map(Artifact.from_string, jars)),
                repos_file,
                license_aliases_file,
                trusted_repos_file,
                opts.write_licenses,
                opts.canonize_licenses,
            )
            logger.info('Done')


def get_repos(opts, repos_file):
    if opts.remote_repos:
        repos = opts.remote_repos
    else:
        try:
            repos = open(repos_file).read().strip().split()
        except Exception:
            repos = []
    logger.debug('Repositories: %s', ' '.join(repos))
    return repos


def split_artifacts_by_type(opts, arcadia, request_artifacts, repos_file):
    import app_ctx  # XXX: use args

    jars, poms = set(), set()
    repos = get_repos(opts, repos_file)
    with tmp.temp_dir() as local_repo:
        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        meta = resolve_transitively(list(map(str, request_artifacts)), local_repo, repos, opts, app_ctx, "check")
    for k, v in meta.items():
        if v == 'bom':
            poms.add(k)
        else:
            jars.add(k)
    return jars, poms


def import_artifacts(
    opts,
    arcadia,
    request_artifacts,
    repos_file,
    license_aliases_file,
    trusted_repos_file,
    write_licenses=False,
    canonize_licenses=False,
    forced_deps=None,
):
    import app_ctx  # XXX: use args

    artifacts_to_import = [a for a in request_artifacts if not arcadia.exists(contrib_location(a))]
    logger.debug('Request artifacts: %s', ' '.join(map(str, artifacts_to_import)))

    if not artifacts_to_import:
        logger.info('Requested artifact(s) exist:\n%s', '\n'.join(contrib_location(a) for a in request_artifacts))
        return

    trusted_repos = []
    if trusted_repos_file and os.path.exists(trusted_repos_file):
        with open(trusted_repos_file) as f:
            trusted_repos = [line.strip() for line in f if line.strip()]
    elif write_licenses:
        logger.error("Licenses will not be written: trusted repos file {} not found".format(trusted_repos_file))

    request_artifacts = artifacts_to_import

    repos = get_repos(opts, repos_file)

    with tmp.temp_dir() as local_repo:
        license_aliases = {}
        if license_aliases_file and os.path.exists(license_aliases_file):
            with open(license_aliases_file) as f:
                try:
                    license_aliases = json.loads(f.read())
                except Exception as e:
                    logger.error("Can't load license aliases file: {} {}".format(license_aliases_file, e))
        else:
            logger.error("License aliases is not available: file {} not found".format(license_aliases_file))

        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        nodes = resolve_transitively(list(map(str, request_artifacts)), local_repo, repos, opts, app_ctx, "jar")

        # For each classifier != sources we create sources node with extended artifactId
        nodes = attach_sources_nodes(nodes)

        nodes = [n for n in nodes if not provides_jwar(n) or not arcadia.exists(contrib_location(n.artifact))]
        if canonize_licenses:
            all_licenses = set()
            for n in nodes:
                if n.licenses:
                    for lic in n.licenses:
                        all_licenses.add(lic)
            all_licenses = {i for i in all_licenses if i not in license_aliases}
            licenses_map = license.collect_licenses_spdx_map(list(all_licenses))
        else:
            licenses_map = {}

        if os.path.exists(license_aliases_file):
            try:
                updated = False
                with open(license_aliases_file) as f:
                    aliases = json.loads(f.read())
                for k, v in licenses_map.items():
                    if k not in aliases:
                        aliases[k] = v
                        updated = True
                if updated:
                    with open(license_aliases_file, 'w') as f:
                        f.write(json.dumps(aliases, sort_keys=True, indent=2))
                    logger.info("License aliases file {} are updated".format(license_aliases_file))
            except Exception as e:
                logger.warning("Can't update license aliases: {}".format(e))

        for n in nodes:
            my_licenses = [licenses_map.get(license_aliases.get(i, i), license_aliases.get(i, i)) for i in n.licenses]
            if write_licenses and n.repository not in trusted_repos:
                logger.warning(
                    "Repository {} (for artifact {}) not included in the list of trusted. Licenses will not be writen automatically".format(
                        n.repository, n.artifact
                    )
                )
                my_licenses = []
            n.licenses = my_licenses

        if opts.local_jar_resources:
            for node in nodes:
                node.resource_id = os.path.basename(node.path)
        else:
            logger.info('Uploading artifacts...')
            upload_artifacts_parallel_and_cacheble(
                list(filter(provides_jwar, nodes)),
                opts,
                app_ctx,
            )

        populate_contrib(
            arcadia,
            nodes,
            owner=opts.contrib_owner,
            dry_run=opts.dry_run,
            write_licenses=write_licenses,
            forced_deps=forced_deps,
            local_jar_resources=opts.local_jar_resources,
        )


def import_bombs(opts, arcadia, request_boms, repos_file, forced_deps=None):
    import app_ctx  # XXX: use args

    artifacts_to_import = [a for a in request_boms if not arcadia.exists(contrib_location_bom(a))]
    logger.debug('Request boms: %s', ' '.join(map(str, artifacts_to_import)))

    if not artifacts_to_import:
        logger.info('Requested bom(s) exist:\n%s', '\n'.join(contrib_location_bom(a) for a in request_boms))
        return

    request_boms = artifacts_to_import

    repos = get_repos(opts, repos_file)

    logger.debug('Repositories: %s', ' '.join(repos))

    with tmp.temp_dir() as local_repo:
        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        meta = resolve_transitively(list(map(str, request_boms)), local_repo, repos, opts, app_ctx, "bom")

    populate_bombs(arcadia, meta, opts.dry_run, forced_deps)


def is_empty_project(data):
    empty_ya_make = data['generate_ya_make'] and not data['jar_file'] and not data['peerdirs']
    empty_inc = (
        data['generate_ya_dependency_management_inc']
        and not data['includes']
        and not data['dm']
        and not data['peerdirs']
    )
    return empty_ya_make, empty_inc


def remove_empty_projects(nodes, empty_ya_makes, empty_incs):
    if not (empty_ya_makes | empty_incs):
        return
    while True:
        try_again = False
        for k, node in nodes.items():
            peers_to_remove = []
            for p in node['peerdirs']:
                if str(p) in empty_ya_makes:
                    peers_to_remove.append(p)
            for p in peers_to_remove:
                node['peerdirs'].remove(p)
            incs_to_remove = []
            for i in node['includes']:
                if str(i) in empty_incs:
                    incs_to_remove.append(i)
            for i in incs_to_remove:
                node['includes'].remove(i)
            empty_ya_make, empty_inc = is_empty_project(node)
            if empty_ya_make:
                empty_ya_makes.add(k)
                node['generate_ya_make'] = False
            if empty_inc:
                empty_incs.add(k)
                node['generate_ya_dependency_management_inc'] = False
            if peers_to_remove or incs_to_remove:
                try_again = True
        if not try_again:
            return


def import_unified(
    opts,
    arcadia,
    request_artifacts,
    repos_file,
    license_aliases_file,
    trusted_repos_file,
    write_licenses=False,
    canonize_licenses=False,
    forced_deps=None,
):
    import app_ctx  # XXX: use args

    forced_deps = forced_deps_as_list(forced_deps)
    artifacts_to_import = [a for a in request_artifacts if not arcadia.exists(contrib_location(a))]
    logger.debug('Request artifacts: %s', ' '.join(map(str, artifacts_to_import)))

    trusted_repos = []
    if trusted_repos_file and os.path.exists(trusted_repos_file):
        with open(trusted_repos_file) as f:
            trusted_repos = [line.strip() for line in f if line.strip()]
    elif write_licenses:
        logger.error("Licenses will not be written: trusted repos file {} not found".format(trusted_repos_file))

    if not artifacts_to_import:
        logger.info('Requested artifact(s) exist:\n%s', '\n'.join(contrib_location(a) for a in request_artifacts))
        return

    request_artifacts = artifacts_to_import

    repos = opts.remote_repos

    with tmp.temp_dir() as local_repo:
        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        meta = resolve_transitively(list(map(str, request_artifacts)), local_repo, repos, opts, app_ctx, 'unified')
        artifacts = meta["ya_models"]
        nodes = {}
        empty_ya_makes, empty_incs = set(), set()

        artifacts = license.enrich_with_licenses(artifacts)
        license_aliases = license.build_licenses_aliases(license_aliases_file, artifacts, canonize_licenses)

        replace_version = opts.replace_version
        for data in artifacts:
            key = extract_artefact(data['artifact'], replace_version)

            my_licenses = []
            if write_licenses and data.get('repository') and data.get('repository') not in trusted_repos:
                logger.warning(
                    "Repository {} (for artifact {}) not included in the list of trusted. Licenses will not be writen automatically".format(
                        data.get('repository'), key
                    )
                )
            else:
                my_licenses = [license_aliases.get(i, i) for i in data.get('licenses', [])]

            nodes[str(key)] = {
                'artifact': key,
                'generate_ya_make': data.get('generate_ya_make', False),
                'generate_ya_dependency_management_inc': data.get('generate_ya_dependency_management_inc', False),
                'pom_file': data.get('pom_file'),
                'jar_file': data.get('jar_file'),
                'source_file': data.get('source_file'),
                'includes': [extract_artefact(i['artifact'], replace_version) for i in data.get('managed_imports', [])],
                'peerdirs': [extract_artefact(i['artifact'], replace_version) for i in data.get('dependencies', [])],
                "excludes": [Artifact.from_unified_dict(i['artifact']) for i in data.get("exclusions", [])],
                'dm': [extract_artefact(i['artifact'], replace_version) for i in data.get('managed_dependencies', [])],
                'licenses': my_licenses,
                'repository': data.get('repository'),
            }
            empty_ya_make, empty_inc = is_empty_project(nodes[str(key)])
            if empty_ya_make:
                empty_ya_makes.add(str(key))
            if empty_inc:
                empty_incs.add(str(key))

        remove_empty_projects(nodes, empty_ya_makes, empty_incs)

        poms, bombs = [], []
        for k, node in nodes.items():
            if node.get('generate_ya_make') and not arcadia.exists(contrib_location(node['artifact'])):
                poms.append(node)
            if node.get('generate_ya_dependency_management_inc') and not arcadia.exists(
                contrib_location_bom(node['artifact'])
            ):
                bombs.append(node)

        if poms:
            if opts.local_jar_resources:
                for pom in poms:
                    if 'jar_file' in pom:
                        pom['jar_file_id'] = os.path.basename(pom['jar_file'])
                    if 'source_file' in pom:
                        pom['source_file_id'] = os.path.basename(pom['source_file'])
            else:
                logger.info('Uploading artifacts...')
                upload_artifacts_parallel_and_cacheble_unified(
                    poms,
                    opts,
                    app_ctx,
                )
            populate_contrib_unified(
                arcadia,
                poms,
                owner=opts.contrib_owner,
                dry_run=opts.dry_run,
                write_licenses=write_licenses,
                forced_deps=forced_deps,
                local_jar_resources=opts.local_jar_resources,
            )
        if bombs:
            populate_bombs_unified(arcadia, bombs, opts.dry_run, forced_deps)

        if "not_imported" in meta:
            contribs = '\n'.join(contrib for contrib in meta["not_imported"])
            logger.info('Managed dependency(ies) not added:\n%s', contribs)


def extract_artefact(raw_artefact, replace_versions: dict):
    artefact = Artifact.from_unified_dict(raw_artefact, set_version=False)

    package_name = str(artefact)
    new_version = replace_versions.get(package_name, raw_artefact.get('version'))
    if new_version:
        artefact.version = new_version
    else:
        raise NoArtefactVersionError("No version specified for artifact '{}'".format(package_name))

    return artefact


def get_peerdir(peerdir, forced_deps):
    peerdir_wo_version = os.path.dirname(peerdir)
    return peerdir_wo_version if peerdir_wo_version in forced_deps else peerdir


def get_dm_line(dep, forced_deps):
    if os.path.dirname(dep) in forced_deps:
        return '    # {} Controlled by FORCED_DEPENDENCY_MANAGEMENT'.format(dep)
    else:
        return '    {}'.format(dep)


def get_forced_deps(params):
    forced_deps = []
    try:
        dump_opts = devtools.ya.core.yarg.merge_opts(FullForcedDepsOptions())
        params = devtools.ya.core.yarg.merge_params(dump_opts.initialize([]), params)
        params.json_forced_deps = True
        lib2lib_with_ver = json.loads(do_forced_deps(params, False))
        forced_deps = lib2lib_with_ver.keys()
    except Exception as e:
        logger.error("Can't get forced dependency management: {}".format(e))
    return forced_deps


def forced_deps_as_list(forced_deps):
    return forced_deps if forced_deps is not None else []
