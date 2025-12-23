import six

import os
import exts.yjson as json
import uuid
import logging
import tempfile
import jinja2

import library.python.filelock
import library.python.resource as rs

import exts.fs as fs
import exts.hashing as hashing
import yalibrary.makelists.macro_definitions as md

import devtools.ya.build.build_opts as bo
import devtools.ya.build.build_handler
import devtools.ya.core.yarg
import devtools.ya.core.config

import devtools.ya.jbuild.gen.gen as gen

import yalibrary.graph.base as graph_base

logger = logging.getLogger(__name__)

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


def get_peerdir(peerdir, forced_deps):
    peerdir_wo_version = os.path.dirname(peerdir)
    return peerdir_wo_version if peerdir_wo_version in forced_deps else peerdir


def get_dm_line(dep, forced_deps):
    if os.path.dirname(dep) in forced_deps:
        return '    # {} Controlled by FORCED_DEPENDENCY_MANAGEMENT'.format(dep)
    else:
        return '    {}'.format(dep)


def forced_deps_as_list(forced_deps):
    return forced_deps if forced_deps is not None else []
