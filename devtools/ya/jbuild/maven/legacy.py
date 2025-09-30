import six

import os
import exts.yjson as json
import uuid
import copy
import logging
import re

import exts.fs as fs
import exts.tmp as tmp
import exts.hashing as hashing
import yalibrary.makelists as ml
import yalibrary.makelists.macro_definitions as md

import devtools.ya.jbuild.gen.base as base
import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.node as jnode
import devtools.ya.jbuild.execute as execute
import devtools.ya.jbuild.commands as commands
import devtools.ya.jbuild.maven.license as license
import devtools.ya.jbuild.maven.version_filter as vf
from devtools.ya.jbuild.maven.utils import (
    ArtifactUploadError,
    contrib_location,
    contrib_location_bom,
    complete_jbuild_opts,
    forced_deps_as_list,
    find_or_create,
    get_contrib_path,
    get_peerdir,
    get_dm_line,
    resolve_transitively,
)

import yalibrary.graph.base as graph_base

logger = logging.getLogger(__name__)

DEFAULT_OWNER = 'g:java-contrib'


def provides_jwar(node):
    return node.path and node.path[-4:] in ('.jar', '.war', '.aar')


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
                token=opts.sandbox_oauth_token,
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


def find_jdk_pattern(resources):
    for resource in resources:
        if re.match('^JDK\\d+-', resource['pattern']) or resource['pattern'].startswith(
            'JDK-'
        ):  # remove "or" after DEVTOOLS-8269 is done
            return '$({})'.format(resource['pattern'])
    return '${}'.format(consts.JDK_DEFAULT_RESOURCE)


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
