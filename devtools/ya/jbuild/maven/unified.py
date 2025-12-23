import six

import os
import exts.yjson as json
import uuid
import logging
import base64

import exts.fs as fs
import exts.tmp as tmp
import exts.hashing as hashing
import yalibrary.makelists as ml
import yalibrary.makelists.macro_definitions as md

import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.node as jnode
import devtools.ya.jbuild.execute as execute
import devtools.ya.jbuild.commands as commands
import devtools.ya.jbuild.maven.license as license
import devtools.ya.jbuild.maven.utils as utils

import devtools.ya.jbuild.maven.version_filter as vf

import yalibrary.graph.base as graph_base

logger = logging.getLogger(__name__)

DEFAULT_OWNER = 'g:java-contrib'


class NoArtefactVersionError(Exception):
    mute = True


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
                token=opts.sandbox_oauth_token,
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
    result = list(by_uid.keys())
    graph = list(by_uid.values())

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
        utils.complete_jbuild_opts(opts),
        app_ctx,
    )

    if rc != 0:
        raise utils.ArtifactUploadError('Can\'t upload one or more artifacts.')

    for pom in poms:
        if pom.get('jar_file'):
            pom['jar_file_id'] = resource_ids[pom['jar_file']]
        if pom.get('source_file'):
            pom['source_file_id'] = resource_ids[pom['source_file']]


def populate_contrib_unified(
    arcadia, origin, owner=None, dry_run=False, write_licenses=False, forced_deps=None, local_jar_resources=False
):
    forced_deps = utils.forced_deps_as_list(forced_deps)
    reachable = {
        utils.get_contrib_path(),
    }
    owner = owner or DEFAULT_OWNER
    poms = {}
    jars = {}

    project_by_path = {}

    # Create arcadia projects, don't write anything
    for p in origin:
        project_path = utils.contrib_location(p['artifact'])
        if arcadia.exists(project_path):
            continue
        project = md.Project('JAVA_CONTRIB')
        project_by_path[project_path] = project

        # SUBSCRIBER
        macro = utils.find_or_create(project, 'SUBSCRIBER')

        if not macro.find_nodes(owner):
            macro.add_value(owner)

        # VERSION
        artifact = p.get('artifact')
        version_macro = utils.find_or_create(project, 'VERSION')
        version_macro.add_value(artifact.version)

        # LICENSE
        if write_licenses and p.get('licenses', []):
            license_macro = utils.find_or_create(project, md.LicenseMacro)
            for lic in p.get('licenses'):
                license_macro.add_value(lic)

        # REPOSITORY
        if p.get('repository'):
            repository_macro = utils.find_or_create(project, 'ORIGINAL_SOURCE')
            repository_macro.children = []
            repository_macro.add_value(p.get('repository'))

        # if p['generate_ya_dependency_management_inc']:
        #     macro = md.StringMacro('INCLUDE')
        #     macro.add_value('${ARCADIA_ROOT}/' + contrib_location_bom(p['artifact']))
        #     project.append_child(macro)

        # PEERDIR
        peerdirs = [utils.contrib_location(d) for d in p.get('peerdirs', [])]

        if peerdirs:
            macro = utils.find_or_create(project, 'PEERDIR')

            for peerdir in peerdirs:
                macro.add_value(utils.get_peerdir(peerdir, forced_deps))

        # EXCLUDE
        if p.get('excludes', []):
            macro = utils.find_or_create(project, 'EXCLUDE')
            for exclude in p.get('excludes', []):
                if exclude.group_id == "*" or exclude.artifactId == "*":
                    continue

                macro.add_value(utils.contrib_location(exclude))

        if p.get('jar_file_id'):
            macro = (
                utils.find_or_create(project, 'LOCAL_JAR')
                if local_jar_resources
                else utils.find_or_create(project, 'JAR_RESOURCE')
            )
            macro.children = []
            macro.add_value(str(p.get('jar_file_id')))
            if local_jar_resources:
                jars[os.path.join(arcadia.arc_root, project_path, p['jar_file_id'])] = p['jar_file']
            if p.get('source_file_id'):
                macro = (
                    utils.find_or_create(project, 'LOCAL_SOURCES_JAR')
                    if local_jar_resources
                    else utils.find_or_create(project, 'SRC_RESOURCE')
                )
                macro.children = []
                macro.add_value(str(p['source_file_id']))
                if local_jar_resources:
                    jars[os.path.join(arcadia.arc_root, project_path, p['source_file_id'])] = p['source_file']

        if p.get('pom_file'):
            poms[project_path] = p['pom_file']

    for project in six.itervalues(project_by_path):
        # END
        utils.find_or_create(project, 'END')

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
        max_peerdir = utils.get_peerdir(graph_base.hacked_path_join(parent_path, max_version), forced_deps)

        if arcadia.exists(parent_path):
            parent_project = arcadia[parent_path].makelist()

            macro = utils.find_or_create(parent_project, 'PEERDIR')
            macro.children = []
            macro.add_value(max_peerdir)
        else:
            parent_project = md.Project('JAVA_CONTRIB_PROXY')
            utils.find_or_create(parent_project, 'SUBSCRIBER').add_value(owner)
            utils.find_or_create(parent_project, 'PEERDIR').add_value(max_peerdir)
            utils.find_or_create(parent_project, 'END')

        parent_project.write(os.path.join(arcadia.arc_root, parent_path, ml.MAKELIST_NAME))

        # Ensure it is recurse reachable
        def ensure_reachability(path):
            if path not in reachable:
                parent_path = os.path.dirname(path)

                ensure_reachability(parent_path)

                if arcadia.exists(parent_path):
                    parent_project = arcadia[parent_path].makelist()

                    macro = utils.find_or_create(parent_project, 'RECURSE')

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
    forced_deps = utils.forced_deps_as_list(forced_deps)
    project_by_path = {}

    # Create arcadia projects, don't write anything
    for p in origin:
        project_path = utils.contrib_location_bom(p['artifact'])
        if arcadia.exists(project_path):
            continue

        parts = []
        for inc in p['includes']:
            parts.append('INCLUDE(${{ARCADIA_ROOT}}/{})'.format(utils.contrib_location_bom(inc)))

        if p['dm']:
            if parts:
                parts.append('\n')
            parts.append('DEPENDENCY_MANAGEMENT(')
            for dep in p['dm']:
                parts.append(utils.get_dm_line(utils.contrib_location(dep), forced_deps))
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

    forced_deps = utils.forced_deps_as_list(forced_deps)
    artifacts_to_import = [a for a in request_artifacts if not arcadia.exists(utils.contrib_location(a))]
    logger.debug('Request artifacts: %s', ' '.join(map(str, artifacts_to_import)))

    trusted_repos = []
    if trusted_repos_file and os.path.exists(trusted_repos_file):
        with open(trusted_repos_file) as f:
            trusted_repos = [line.strip() for line in f if line.strip()]
    elif write_licenses:
        logger.error("Licenses will not be written: trusted repos file {} not found".format(trusted_repos_file))

    if not artifacts_to_import:
        logger.info('Requested artifact(s) exist:\n%s', '\n'.join(utils.contrib_location(a) for a in request_artifacts))
        return

    request_artifacts = artifacts_to_import

    repos = opts.remote_repos

    with tmp.temp_dir() as local_repo:
        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        meta = utils.resolve_transitively(
            list(map(str, request_artifacts)), local_repo, repos, opts, app_ctx, 'unified'
        )
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
                "excludes": [utils.Artifact.from_unified_dict(i['artifact']) for i in data.get("exclusions", [])],
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
            if node.get('generate_ya_make') and not arcadia.exists(utils.contrib_location(node['artifact'])):
                poms.append(node)
            if node.get('generate_ya_dependency_management_inc') and not arcadia.exists(
                utils.contrib_location_bom(node['artifact'])
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
    artefact = utils.Artifact.from_unified_dict(raw_artefact, set_version=False)

    package_name = str(artefact)
    new_version = replace_versions.get(package_name, raw_artefact.get('version'))
    if new_version:
        artefact.version = new_version
    else:
        raise NoArtefactVersionError("No version specified for artifact '{}'".format(package_name))

    return artefact
