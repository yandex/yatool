import os
import exts.yjson as json
import logging

import exts.tmp as tmp
import yalibrary.makelists as ml

import devtools.ya.build.build_handler
import devtools.ya.core.yarg
import devtools.ya.core.config
from devtools.ya.handlers.dump import FullForcedDepsOptions, do_forced_deps

import devtools.ya.jbuild.maven.legacy as legacy
import devtools.ya.jbuild.maven.unified as unified
import devtools.ya.jbuild.maven.utils as utils
from devtools.ya.jbuild.maven.utils import Artifact

import yalibrary.graph.base as graph_base

logger = logging.getLogger(__name__)

IMPORTER_PATH = graph_base.hacked_path_join('devtools', 'maven-import')
IMPORTER_MAIN = 'ru.yandex.devtools.maven.importer.Importer'
DEFAULT_OWNER = 'g:java-contrib'


def do_import(opts):
    forced_deps = get_forced_deps(opts)
    arcadia = ml.Arcadia(opts.arc_root)
    contrib = os.path.join(opts.arc_root, utils.get_contrib_path())
    repos_file = os.path.join(contrib, 'MAVEN_REPOS')
    license_aliases_file = os.path.join(contrib, 'LICENSE_ALIASES')
    trusted_repos_file = os.path.join(contrib, 'TRUSTED_REPOS')
    request_artifacts = list(map(Artifact.from_string, set(opts.libs)))
    if opts.unified_mode:
        logger.info('Unified importer used')
        logger.info('Try to import: ' + ' '.join(set(opts.libs)))
        unified.import_unified(
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
            legacy.import_bombs(opts, arcadia, list(map(Artifact.from_string, poms)), repos_file, forced_deps)
            logger.info('Done')
        if jars:
            logger.info('Try to import artifacts: ' + ' '.join(jars))
            legacy.import_artifacts(
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


def split_artifacts_by_type(opts, arcadia, request_artifacts, repos_file):
    import app_ctx  # XXX: use args

    jars, poms = set(), set()
    repos = utils.get_repos(opts, repos_file)
    with tmp.temp_dir() as local_repo:
        local_repo = os.environ.get('YA_JBUILD_LOCAL_MAVEN_REPO', local_repo)
        logger.debug('Local repository: %s', local_repo)

        logger.info('Resolving artifacts...')
        meta = utils.resolve_transitively(list(map(str, request_artifacts)), local_repo, repos, opts, app_ctx, "check")
    for k, v in meta.items():
        if v == 'bom':
            poms.add(k)
        else:
            jars.add(k)
    return jars, poms


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
