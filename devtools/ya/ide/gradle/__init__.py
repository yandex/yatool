import json
import logging
import os
import shutil
import subprocess
import sys
import tempfile

from build.ymake2 import run_ymake
import build.graph as bg
import core.yarg
from core import stage_tracer
import build.build_opts as bo
import yalibrary.tools
from build import build_facade, ya_make
import yalibrary.platform_matcher as pm

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("gradle")
additional_files_ext = ['.config', '.make', '.yaml']
requires_props = [
    'bucketUsername',
    'bucketPassword',
    'systemProp.gradle.wrapperUser',
    'systemProp.gradle' '.wrapperPassword',
]
user_root = os.path.expanduser("~")
gradle_props = os.path.join(user_root, '.gradle', 'gradle.properties')
build_libs_subdir = '.hic_sunt_dracones'


def in_rel_targets(rel_target, rel_targets_with_slash):
    for rel_target_with_slash in rel_targets_with_slash:
        if rel_target[: len(rel_target_with_slash)] == rel_target_with_slash:
            return True
    return False


class NodeValidator:
    def __init__(self, rel_targets_with_slash, arcadia_root):
        self.validate_funcs = [self._bundle_validator, self._proto_files_validator, self._proto_deps_sources_validator]
        self.additional_targets = []
        self.outside_targets = []
        self.rel_targets_with_slash = rel_targets_with_slash
        self.arcadia_root = arcadia_root

    def validate(self, node):
        success = False
        for func in self.validate_funcs:
            if func(node):
                success = True
        return success, self.additional_targets, self.outside_targets

    @staticmethod
    def _bundle_validator(node):
        if node.get('NodeType', None) == 'Bundle' and 'semantics' in node:
            return True
        return False

    def _proto_files_validator(self, node):
        if node.get('NodeType', None) == 'NonParsedFile' and 'semantics' in node:
            for semantic in node['semantics']:
                if 'sem' in semantic and len(semantic['sem']) == 2 and semantic['sem'][0] == 'proto_files':
                    proto_sem = semantic['sem'][1]
                    if in_rel_targets(proto_sem, self.rel_targets_with_slash):
                        self.additional_targets.append(semantic['sem'][1])
                        return True

        return False

    def _proto_deps_sources_validator(self, node):
        if node.get('NodeType', None) == 'Bundle' and 'semantics' in node:
            for semantic in node['semantics']:
                if 'sem' in semantic and len(semantic['sem']) == 3 and semantic['sem'][0] == 'jar_proto':
                    self.outside_targets.extend(
                        find_files_by_extension(self.arcadia_root, semantic['sem'][1], [".proto"])
                    )
                    return True
        return False


def find_files_by_extension(arcadia_root, start_dir, extensions):
    result = []
    for root, dirs, files in os.walk(os.path.join(arcadia_root, start_dir)):
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                result.append(os.path.join(start_dir, file))
    return result


def save_bucket_creds(login, token):
    props = {}
    if os.path.isfile(gradle_props):
        with open(gradle_props) as f:
            lines = f.readlines()
        for line in lines:
            line = line.strip()
            if line and not line.startswith('#'):
                key, value = line.split('=')
                props[key.strip()] = value.strip()
    for idx, prop in enumerate(requires_props):
        if idx % 2 == 0 and login is not None:
            props[prop] = login
        elif token is not None:
            props[prop] = token
        if prop not in props:
            return False
    with open(gradle_props, 'w') as f:
        for k, v in props.items():
            f.write('{}={}\n'.format(k, v))
    return True


def check_bucket_creds():
    if not os.path.isfile(gradle_props):
        return False, 'file gradle.properties does not exist'
    with open(gradle_props) as f:
        props = f.read()
    for p in requires_props:
        if p not in props:
            return False, 'property {} is not defined in gradle.properties file'.format(p)
    return True, ''


def is_subpath_of(path, root_path):
    return os.path.realpath(path).startswith(os.path.realpath(root_path) + os.sep)


def recursive_symlinks(arcadia_root, gradle_project_root, target):
    arc_target_src = os.path.join(arcadia_root, target)
    exp_target_src = os.path.join(gradle_project_root, target)
    if not os.path.exists(arc_target_src):
        return
    # pay attention to it
    if not os.path.exists(exp_target_src):
        os.makedirs(os.path.dirname(exp_target_src), exist_ok=True)
        os.symlink(os.path.relpath(arc_target_src, os.path.dirname(exp_target_src)), exp_target_src)
    elif os.path.isdir(arc_target_src):
        for path in os.listdir(arc_target_src):
            if os.path.isdir(os.path.join(arc_target_src, path)):
                recursive_symlinks(arcadia_root, gradle_project_root, os.path.join(target, path))


def apply_graph(params, sem_graph, gradle_project_root):
    with open(sem_graph) as f:
        graph = json.load(f)
        f.close()

    rel_targets_with_slash = []  # Relative targets for export as source to gradle project with slash at end
    for rel_target in params.rel_targets:
        if '/' == rel_target[-1:]:
            rel_targets_with_slash.append(rel_target)
        else:
            rel_targets_with_slash.append(rel_target + '/')

    arcadia_root = params.arc_root
    project_outside_arcadia = not is_subpath_of(gradle_project_root, arcadia_root)
    build_rel_targets = []  # Relative paths to targets for build
    for node in graph['data']:
        validated, additional_targets, outside_targets = NodeValidator(rel_targets_with_slash, arcadia_root).validate(
            node
        )
        if not validated:
            continue

        for outside_target in outside_targets:
            if not in_rel_targets(outside_target, rel_targets_with_slash):
                recursive_symlinks(arcadia_root, gradle_project_root, outside_target)

        rel_target = node['Name'].replace('$B/', '').replace('$S/', '')  # Relative target - some *.jar
        if in_rel_targets(rel_target, rel_targets_with_slash):
            if project_outside_arcadia:
                # Target for export as sources, make symlinks to all sources in export folder
                rel_target_srcs = [os.path.join(os.path.dirname(rel_target), 'src')]
                rel_target_srcs.extend(additional_targets)
                # TODO Add other non-standard sources from jar_source_set semantic to rel_target_srcs
                for rel_target_src in rel_target_srcs:
                    recursive_symlinks(arcadia_root, gradle_project_root, rel_target_src)
        elif params.build_contribs:
            build_rel_targets.append(rel_target)
        else:
            contrib = False
            for semantic in node['semantics']:
                if (
                    'sem' in semantic
                    and len(semantic['sem']) >= 2
                    and semantic['sem'][0] == 'consumer-type'
                    and semantic['sem'][1] == 'contrib'
                ):
                    contrib = True
                    break
            if not contrib:
                build_rel_targets.append(rel_target)

    # .config, .make, .yaml etc files symlinks addition
    add_files_targets = find_files_by_extension(arcadia_root, params.rel_targets[0], additional_files_ext)
    for target in add_files_targets:
        recursive_symlinks(arcadia_root, gradle_project_root, target)

    if len(build_rel_targets):  # Has something to build
        import app_ctx

        ya_make_opts = core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
        opts = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra))

        opts.bld_dir = params.bld_dir
        opts.arc_root = arcadia_root
        opts.bld_root = params.bld_root

        opts.rel_targets = list()
        opts.abs_targets = list()
        for build_rel_target in build_rel_targets:  # Add all targets for build simultaneously
            rel_dir = os.path.dirname(build_rel_target)
            opts.rel_targets.append(rel_dir)
            opts.abs_targets.append(os.path.join(arcadia_root, rel_dir))

        logger.info("Making building graph with opts\n")
        with app_ctx.event_queue.subscription_scope(ya_make.DisplayMessageSubscriber(opts, app_ctx.display)):
            graph, _, _, _, _ = bg.build_graph_and_tests(opts, check=True, display=app_ctx.display)

        builder = ya_make.YaMake(opts, app_ctx, graph=graph, tests=[])
        exit_code = builder.go()
        if exit_code != 0:
            sys.exit(exit_code)

        if project_outside_arcadia:
            # Make symlinks to all built targets
            for build_rel_target in build_rel_targets:
                dst = os.path.join(os.path.join(gradle_project_root, build_libs_subdir), build_rel_target)
                if os.path.exists(dst):
                    os.unlink(dst)
                src = os.path.join(arcadia_root, build_rel_target)
                if os.path.exists(src):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    os.symlink(src, dst)


def do_gradle(params):
    do_gradle_stage = stager.start('do_gradle')

    if pm.my_platform() == 'win32':
        logger.error("Win is not supported in ya ide gradle")
        return

    check, err = check_bucket_creds()
    if not check:
        logger.error(
            'Bucket credentials error: {}\n'
            'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket/gradle#autentifikaciya\n'
            'Token can be taken from here '
            'https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b'.format(
                err
            )
        )
        return

    arcadia_root = params.arc_root

    # If not defined any target use current directory as target
    if len(params.abs_targets) == 0:
        abs_target = os.path.realpath(os.getcwd())
        params.abs_targets.append(abs_target)
        params.rel_targets.append(os.path.relpath(abs_target, arcadia_root))

    gradle_project_root = params.gradle_project_root
    if gradle_project_root is None:
        if len(params.abs_targets) > 1:
            logger.error("Must be defined --project-root when used few targets")
            return
        # For single target use it as project_root, when not defined
        gradle_project_root = os.path.realpath(params.abs_targets[0])

    logger.info("Project root: " + gradle_project_root)

    project_outside_arcadia = not is_subpath_of(gradle_project_root, arcadia_root)
    tmp = None
    if project_outside_arcadia:
        # Save handler files to project root
        handler_root = gradle_project_root
    else:
        # Save handler files to tmp
        tmp = tempfile.mkdtemp()
        handler_root = tmp
        logger.info("Handler root: " + handler_root)

    flags = {
        'EXPORTED_BUILD_SYSTEM_SOURCE_ROOT': arcadia_root,
        'EXPORTED_BUILD_SYSTEM_BUILD_ROOT': gradle_project_root,
        'EXPORT_GRADLE': 'yes',
        'TRAVERSE_RECURSE': 'yes',
        'TRAVERSE_RECURSE_FOR_TESTS': 'yes',
        'BUILD_LANGUAGES': 'JAVA',  # KOTLIN == JAVA
        'USE_PREBUILT_TOOLS': 'no',
    }
    params.flags.update(flags)
    # to avoid problems with proto
    params.ya_make_extra.append('-DBUILD_LANGUAGES=JAVA')

    conf = build_facade.gen_conf(
        build_root=handler_root,
        build_type='nobuild',
        build_targets=params.abs_targets,
        flags=params.flags,
        ymake_bin=getattr(params, 'ymake_bin', None),
        host_platform=params.host_platform,
        target_platforms=params.target_platforms,
        arc_root=arcadia_root,
    )

    yexport = yalibrary.tools.tool('yexport') if params.yexport_bin is None else params.yexport_bin
    ymake = yalibrary.tools.tool('ymake') if params.ymake_bin is None else params.ymake_bin

    ymake_args = [
        '-k',
        '--build-root',
        gradle_project_root,
        '--config',
        conf,
        '--plugins-root',
        os.path.join(arcadia_root, 'build/plugins'),
        '--xs',
        '--sem-graph',
    ] + params.abs_targets

    def listener(event):
        logger.info(event)

    logger.info("Generate sem-graph command:\n" + ' '.join([ymake] + ymake_args) + "\n")

    _, stdout, _ = run_ymake.run(ymake, ymake_args, {}, listener, raw_cpp_stdout=False)

    shutil.copy(
        conf, os.path.join(handler_root, 'ymake.conf')
    )  # TODO Remove For debugonly, required for retry to make sem.json
    sem_graph = os.path.join(handler_root, 'sem.json')
    with open(sem_graph, 'w') as f:
        f.write(stdout)
        f.close()

    if params.gradle_name:
        project_name = params.gradle_name
    else:
        project_name = params.abs_targets[0].split(os.sep)[-1]

    logger.info("Path prefixes for skip in yexport:\n" + ' '.join(params.rel_targets) + "\n")

    yexport_toml = os.path.join(handler_root, 'yexport.toml')
    with open(yexport_toml, 'w') as f:
        f.write(
            '[add_attrs.dir]\n'
            + 'build_contribs = '
            + ('true' if params.build_contribs else 'false')
            + '\n'
            + '[[target_replacements]]\n'
            + 'skip_path_prefixes = [ "'
            + '", "'.join(params.rel_targets)
            + '" ]\n'
            + '\n'
            + '[[target_replacements.addition]]\n'
            + 'name = "consumer-prebuilt"\n'
            + 'args = []\n'
            + '[[target_replacements.addition]]\n'
            + 'name = "IGNORED"\n'
            + 'args = []\n'
        )
        f.close()

    yexport_args = [
        yexport,
        '--arcadia-root',
        arcadia_root,
        '--export-root',
        gradle_project_root,
        '--configuration',
        handler_root,
        '--semantic-graph',
        sem_graph,
        '--generator',
        'ide-gradle',
        '--target',
        project_name,
    ]

    logger.info("Generate by yexport command:\n" + ' '.join(yexport_args) + "\n")

    subprocess.call(yexport_args)

    apply_graph(params, sem_graph, gradle_project_root)

    # TODO remove ymake.conf, sem.json, yexport.toml

    do_gradle_stage.finish()

    if tmp:
        shutil.rmtree(tmp)
