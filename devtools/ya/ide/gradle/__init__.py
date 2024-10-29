import logging
import os
import shutil
import subprocess
import sys
import hashlib
from typing import Callable
from pathlib import Path

from build.ymake2 import run_ymake
import build.graph as bg
import core.yarg
import core.config
from core import stage_tracer
import build.build_opts as bo
import yalibrary.tools
from devtools.ya.yalibrary import sjson
from build import build_facade, ya_make
import yalibrary.platform_matcher as pm

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("gradle")


requires_props = [
    'bucketUsername',
    'bucketPassword',
    'systemProp.gradle.wrapperUser',
    'systemProp.gradle' '.wrapperPassword',
]
gradle_props_file = Path.home() / '.gradle' / 'gradle.properties'

GRADLE_DIR = ".gradle"
SETTINGS_FILES = ["settings.gradle.kts", "gradlew", "gradlew.bat"]  # Files for symlink to settings root
SETTINGS_DIRS = [GRADLE_DIR, ".idea", "gradle"]  # Folders for symlink to settings root

YMAKE_DIR = "ymake"
SKIP_ROOT_DIRS = SETTINGS_DIRS + [YMAKE_DIR]  # Skipped for build directories in export root
BUILD_FILE = "build.gradle.kts"

NODE_NAME = "Name"  # Build target of node
NODE_SEMANTICS = "semantics"  # List of node semantics
NODE_SEMANTIC = "sem"  # One node semantic


def _check_bucket_creds() -> bool:
    """Check exists all required gradle properties"""
    errors = []
    if not gradle_props_file.is_file():
        errors.append(f'file {gradle_props_file} does not exist')
    else:
        with gradle_props_file.open() as f:
            props = f.read()
        for p in requires_props:
            if p not in props:
                errors.append(f'property {p} is not defined in gradle.properties file')
    if errors:
        logger.error(
            'Bucket credentials error: %s\n'
            'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket/gradle#autentifikaciya\n'
            'Token can be taken from here '
            'https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b',
            ', '.join(errors),
        )
        return False
    return True


def _prepare_input_targets(params, arcadia_root: Path) -> None:
    """Add current directory to input targets, if targets empty"""
    if not params.abs_targets:
        abs_target = Path.cwd().resolve()
        params.abs_targets.append(str(abs_target))
        params.rel_targets.append(os.path.relpath(abs_target, arcadia_root))
    logger.info("Input targets: %s", ', '.join(params.rel_targets))


def _get_export_root(params) -> Path:
    """Create export_root path by hash of targets"""
    targets_hash = hashlib.sha1(':'.join(sorted(params.abs_targets)).encode("utf-8")).hexdigest()
    export_root = Path.home() / ".ya" / "gradle" / targets_hash
    logger.info("Export root: %s", export_root)
    return export_root


def _get_settings_root(params, arcadia_root: Path) -> Path:
    """Create settings_root path by options and targets"""
    if params.settings_root is None:
        settings_root = Path(params.abs_targets[0])
    else:
        settings_root = arcadia_root / params.settings_root
    if not settings_root.exists() or not settings_root.is_dir():
        logger.error('Not found settings directory %s', settings_root)
        return
    logger.info("Settings root: %s", settings_root)
    return settings_root


def _remove_symlink(export_file: Path, arcadia_file: Path) -> None:
    """Remove symlink from arcadia_file to export_file"""
    if arcadia_file.is_symlink() and arcadia_file.resolve() == export_file:
        try:
            arcadia_file.unlink()
        except Exception as e:
            logger.warning("Can't remove symlink '%s' -> '%s': %s", arcadia_file, export_file, str(e))


def _make_symlink(export_file: Path, arcadia_file: Path) -> None:
    """Make symlink from arcadia_file to export_file"""
    try:
        _remove_symlink(export_file, arcadia_file)
        arcadia_file.symlink_to(export_file, export_file.is_dir())
    except Exception as e:
        logger.warning("Can't create symlink '%s' -> '%s': %s", arcadia_file, export_file, str(e))


def _apply_settings_symlinks(export_root: Path, settings_root: Path, on_symlink: Callable[[Path, Path], None]) -> None:
    """Apply on_symlink function for each settings files/dirs"""
    (export_root / GRADLE_DIR).mkdir(0o755, parents=True, exist_ok=True)
    for export_file in export_root.iterdir():
        basename = export_file.name
        if (basename in SETTINGS_FILES and export_file.is_file()) or (
            basename in SETTINGS_DIRS and export_file.is_dir()
        ):
            arcadia_file = settings_root / basename
            on_symlink(export_file, arcadia_file)


def _apply_build_symlinks_recursive(
    export_root: Path, arcadia_root: Path, rel_dir: Path, on_symlink: Callable[[Path, Path], None]
) -> None:
    """Recursive apply on_symlink function for each build files/dirs from arcadia to export"""
    export_dir = export_root / rel_dir
    for export_file in export_dir.iterdir():
        if export_file.name == BUILD_FILE and export_file.is_file():
            arcadia_file = arcadia_root / rel_dir / Path(export_file.name)
            on_symlink(export_file, arcadia_file)
        elif export_file.is_dir():
            _apply_build_symlinks_recursive(export_root, arcadia_root, Path(rel_dir, export_file.name), on_symlink)


def _apply_build_symlinks(export_root: Path, arcadia_root: Path, on_symlink: Callable[[Path, Path], None]) -> None:
    """Apply on_symlink function for each build files/dirs from arcadia to export"""
    for export_file in export_root.iterdir():
        basename = export_file.name
        if basename not in SKIP_ROOT_DIRS and export_file.is_dir():
            _apply_build_symlinks_recursive(export_root, arcadia_root, Path(export_file.name), on_symlink)
        elif basename == BUILD_FILE and export_file.is_file():
            on_symlink(export_file, arcadia_root / basename)


def _clear_export(export_root: Path, arcadia_root: Path, settings_root: Path) -> None:
    """Clear export_root and arcadia symlinks before rebuild export"""
    if not export_root.exists():
        return
    _apply_settings_symlinks(
        export_root, settings_root, _remove_symlink
    )  # Remove settings symlinks from arcadia to export
    _apply_build_symlinks(export_root, arcadia_root, _remove_symlink)  # Remove build symlinks from arcadia to export
    shutil.rmtree(export_root)


def _read_sem_graph(sem_graph: Path) -> dict:
    try:
        with sem_graph.open('rb') as f:
            graph = sjson.load(f)
        return graph
    except Exception as e:
        logger.error("Fail read sem-graph from `%s`: %s", str(sem_graph), str(e))
        return None


def _node_with_semantics(node: dict) -> bool:
    return NODE_SEMANTICS in node


def _is_valid_semgraph_node(node: dict) -> bool:
    valid = True
    if NODE_NAME not in node or not isinstance(node[NODE_SEMANTICS], list) or not node[NODE_SEMANTICS]:
        valid = False
    for semantic in node[NODE_SEMANTICS]:
        if (
            NODE_SEMANTIC not in semantic
            or not isinstance(semantic[NODE_SEMANTIC], list)
            or not semantic[NODE_SEMANTIC]
        ):
            valid = False
            break
    if not valid:
        logger.error('Skip invalid sem-graph node %s', str(sjson.dumps(node)))
    return valid


def _find_run_java_program(sem_graph: Path, export_root: Path, arcadia_root: Path) -> list[Path]:
    """Find RUN_JAVA_PROGRAMs in sem-graph and extract additional targets for they"""
    graph = _read_sem_graph(sem_graph)
    if graph is None:
        return []

    try:
        additional_targets = []
        for node in graph['data']:
            if not _node_with_semantics(node) or not _is_valid_semgraph_node(node):
                continue
            semantics = node[NODE_SEMANTICS]
            if (
                len(semantics) >= 3
                and semantics[0][NODE_SEMANTIC][0] == 'runs-ITEM'
                and len(semantics[2][NODE_SEMANTIC]) >= 2
                and semantics[2][NODE_SEMANTIC][1] != '@.cplst'
            ):
                additional_target = semantics[2][NODE_SEMANTIC][1]
                additional_target = additional_target.replace('@', '')
                additional_target = Path(additional_target).parent
                additional_target = os.path.relpath(additional_target, export_root)
                additional_target = arcadia_root / additional_target
                additional_targets.append(additional_target)
    except Exception as e:
        logger.error("Fail extract additional targets from sem-graph: %s", str(e))

    return additional_targets


def _make_sem_graph(ymake, params, arcadia_root: Path, ymake_root: Path, listener: Callable) -> Path:
    """Make sem-graph to ymake_root"""
    conf = build_facade.gen_conf(
        build_root=core.config.build_root(),
        build_type='nobuild',
        build_targets=params.abs_targets,
        flags=params.flags,
        ymake_bin=getattr(params, 'ymake_bin', None),
        host_platform=params.host_platform,
        target_platforms=params.target_platforms,
        arc_root=arcadia_root,
    )

    ymake_root.mkdir(0o755, parents=True, exist_ok=True)
    ymake_conf = ymake_root / 'ymake.conf'
    shutil.copy(conf, ymake_conf)

    ymake_args = [
        '-k',
        '--build-root',
        str(ymake_root),
        '--config',
        str(ymake_conf),
        '--plugins-root',
        str(arcadia_root / 'build' / 'plugins'),
        '--xs',
        '--sem-graph',
    ] + params.abs_targets

    logger.info("Generate sem-graph command:\n%s", ' '.join([ymake] + ymake_args))

    exit_code, stdout, stderr = run_ymake.run(ymake, ymake_args, {}, listener, raw_cpp_stdout=False)

    if exit_code != 0:
        logger.error("Fail generate sem-graph:\n%s", stderr)
        return None

    sem_graph = ymake_root / 'sem.json'
    with sem_graph.open('w') as f:
        f.write(stdout)

    return sem_graph


def _generate_sem_graph(params, export_root: Path, arcadia_root: Path, ymake_root: Path) -> list[Path, list[Path]]:
    flags = {
        'EXPORTED_BUILD_SYSTEM_SOURCE_ROOT': str(arcadia_root),
        'EXPORTED_BUILD_SYSTEM_BUILD_ROOT': str(export_root),
        'YA_IDE_GRADLE': 'yes',
        'EXPORT_GRADLE': 'yes',
        'TRAVERSE_RECURSE': 'yes',
        'TRAVERSE_RECURSE_FOR_TESTS': 'yes',
        'BUILD_LANGUAGES': 'JAVA',  # KOTLIN == JAVA
        'USE_PREBUILT_TOOLS': 'no',
    }
    params.flags.update(flags)
    params.ya_make_extra.append('-DBUILD_LANGUAGES=JAVA')  # to avoid problems with proto

    ymake = yalibrary.tools.tool('ymake') if params.ymake_bin is None else params.ymake_bin

    def listener(event):
        logger.info(event)

    sem_graph = _make_sem_graph(ymake, params, arcadia_root, ymake_root, listener)
    if sem_graph is None:
        return None, None

    additional_run_java_program_targets = _find_run_java_program(sem_graph, export_root, arcadia_root)
    additional_run_java_program_rel_targets = list()
    if additional_run_java_program_targets:
        for additional_run_java_program_target in additional_run_java_program_targets:
            params.abs_targets.append(str(additional_run_java_program_target))
            rel_target = os.path.relpath(additional_run_java_program_target, arcadia_root)
            params.rel_targets.append(str(rel_target))
            additional_run_java_program_rel_targets.append(rel_target)
        sem_graph = _make_sem_graph(ymake, params, arcadia_root, ymake_root, listener)
        if sem_graph is None:
            return None, None
        logger.info("Updated by targets: %s", ', '.join(params.rel_targets))

    return sem_graph, additional_run_java_program_rel_targets


def _generate_by_yexport(params, export_root: Path, arcadia_root: Path, ymake_root: Path, sem_graph: Path) -> bool:
    if params.gradle_name:
        project_name = params.gradle_name
    else:
        project_name = params.abs_targets[0].split(os.sep)[-1]

    logger.info("Path prefixes for skip in yexport:\n%s", ' '.join(params.rel_targets))

    yexport_toml = ymake_root / 'yexport.toml'
    with yexport_toml.open('w') as f:
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

    yexport = yalibrary.tools.tool('yexport') if params.yexport_bin is None else params.yexport_bin

    yexport_args = [
        yexport,
        '--arcadia-root',
        str(arcadia_root),
        '--export-root',
        str(export_root),
        '--configuration',
        str(ymake_root),
        '--semantic-graph',
        str(sem_graph),
    ]
    if params.yexport_debug_mode is not None:
        yexport_args += ["--debug-mode", str(params.yexport_debug_mode)]
    yexport_args += [
        '--generator',
        'ide-gradle',
        '--target',
        project_name,
    ]

    logger.info("Generate by yexport command:\n%s", ' '.join(yexport_args))
    r = subprocess.run(yexport_args, capture_output=True, text=True)
    if r.returncode != 0:
        logger.error("Fail yexport command:\n%s", r.stderr)
        return False
    return True


def _build_targets(params, sem_graph: Path, additional_run_java_program_rel_targets: list[Path]) -> bool:
    """Extract build targets from sem-graph and build they"""
    graph = _read_sem_graph(sem_graph)
    if graph is None:
        return False

    try:
        build_rel_targets = additional_run_java_program_rel_targets
        for node in graph['data']:
            if not _node_with_semantics(node) or not _is_valid_semgraph_node(node):
                continue

            name = node['Name']
            if not name.startswith('$B/') or not name.endswith('.jar'):  # Search only *.jar with semantics
                continue

            rel_target = Path(name.replace('$B/', '')).parent  # Relative target - directory of *.jar
            in_rel_targets = False
            for input_rel_target in params.rel_targets:
                if rel_target.is_relative_to(Path(input_rel_target)):
                    in_rel_targets = True
                    break

            if in_rel_targets:
                # Skip target, already in input targets
                continue
            elif params.build_contribs:
                # Build all non-input targets
                build_rel_targets.append(rel_target)
            else:
                # else build all non-contrib targets
                contrib = False
                for semantic in node[NODE_SEMANTICS]:
                    sem = semantic[NODE_SEMANTIC]
                    if len(sem) == 2 and sem[0] == 'consumer-type' and sem[1] == 'contrib':
                        contrib = True
                        break
                if not contrib:
                    build_rel_targets.append(rel_target)
    except Exception as e:
        logger.error("Fail extract build targets from sem-graph `%s`: %s", str(sem_graph), str(e))
        return False

    if build_rel_targets:  # Has something for build
        import app_ctx

        try:
            ya_make_opts = core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
            opts = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra))

            arcadia_root = Path(params.arc_root)

            opts.bld_dir = params.bld_dir
            opts.arc_root = str(arcadia_root)
            opts.bld_root = params.bld_root

            opts.rel_targets = list()
            opts.abs_targets = list()
            for build_rel_target in build_rel_targets:  # Add all targets for build simultaneously
                opts.rel_targets.append(str(build_rel_target))
                opts.abs_targets.append(str(arcadia_root / build_rel_target))

            logger.info("Making building graph")
            with app_ctx.event_queue.subscription_scope(ya_make.DisplayMessageSubscriber(opts, app_ctx.display)):
                graph, _, _, _, _ = bg.build_graph_and_tests(opts, check=True, display=app_ctx.display)
            logger.info("Build all by graph")
            builder = ya_make.YaMake(opts, app_ctx, graph=graph, tests=[])
            return_code = builder.go()
            if return_code != 0:
                logger.error("Some builds failed")
                return False
        except Exception as e:
            logger.error("Failed in build process: %s", str(e))
            return False
    return True


def do_gradle(params):
    """Real handler of `ya ide gradle`"""
    return_code = 1  # By default some failed
    do_gradle_stage = stager.start('do_gradle')

    while True:

        if pm.my_platform() == 'win32':
            logger.error("Windows is not supported in ya ide gradle")
            break

        if not _check_bucket_creds():
            break

        arcadia_root = Path(params.arc_root)
        _prepare_input_targets(params, arcadia_root)
        export_root = _get_export_root(params)
        settings_root = _get_settings_root(params, arcadia_root)
        _clear_export(export_root, arcadia_root, settings_root)

        ymake_root = export_root / YMAKE_DIR
        sem_graph, additional_run_java_program_rel_targets = _generate_sem_graph(
            params, export_root, arcadia_root, ymake_root
        )
        if sem_graph is None:
            break

        if not _generate_by_yexport(params, export_root, arcadia_root, ymake_root, sem_graph):
            break

        _apply_settings_symlinks(
            export_root, settings_root, _make_symlink
        )  # Make settings symlinks from arcadia to export
        _apply_build_symlinks(export_root, arcadia_root, _make_symlink)  # Make build symlinks from arcadia to export

        if not _build_targets(params, sem_graph, additional_run_java_program_rel_targets):
            break

        return_code = 0  # All executed success
        break

    do_gradle_stage.finish()

    sys.exit(return_code)
