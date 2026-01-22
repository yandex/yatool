import logging
import os
import subprocess
import sys
import typing as tp

from collections.abc import Generator, Callable, Iterable, Sequence
from pathlib import Path, PurePath

import devtools.ya.handlers.style.state_helper as state_helper
import devtools.ya.handlers.style.styler as stlr


STDIN_FILENAME = 'source.cpp'

logger = logging.getLogger(__name__)


class MineOptions(tp.NamedTuple):
    targets: tuple[Path, ...]
    file_types: tuple[stlr.StylerKind, ...] = ()
    stdin_filename: str = STDIN_FILENAME
    tty: bool = os.isatty(sys.stdin.fileno())
    enable_implicit_taxi_formatters: bool = False
    paths_with_integrations: tuple[str, ...] = ()
    smart: bool = False
    smart_staged: bool = False


class Target(tp.NamedTuple):
    path: Path | PurePath
    reader: Callable[..., str]
    stdin: bool = False
    passed_directly: bool = False


def _mine_filepath_targets(paths: Iterable[Path]) -> Generator[Target]:
    for path in paths:
        if path.is_symlink():
            continue
        if path.is_file():
            yield Target(path.resolve(), path.read_text, passed_directly=True)
        elif path.is_dir():
            path = path.resolve()
            for dirpath, _, filenames in path.walk():
                if dirpath.is_symlink():
                    continue

                for filename in filenames:
                    filepath = dirpath / filename
                    if filepath.is_symlink():
                        continue
                    yield Target(filepath, filepath.read_text)
        else:
            logger.warning('skip %s (no such file or directory)', path)


def _get_vcs_root(path: Path) -> tuple[str, Path]:
    path = path.resolve(strict=True)
    if not path.is_dir():
        path = path.parent
    for vcs in ('arc', 'git'):
        process = subprocess.run(
            [vcs, 'rev-parse', '--show-toplevel'],
            stdout=subprocess.PIPE,
            encoding='utf-8',
            cwd=path,
        )
        if process.returncode == 0:
            return vcs, Path(process.stdout.strip())
    raise ValueError(f'Target {path} is not under git/arc repo')


def _mine_targets_smart(paths: Sequence[Path], only_staged: bool = False) -> Generator[Target]:
    if not paths:
        return
    # XXX: Assuming all paths are under the same repo
    vcs, root = _get_vcs_root(paths[0])
    for path in paths:
        output = subprocess.check_output(
            [vcs, 'diff', '--staged' if only_staged else 'HEAD', '--name-status', path],
            encoding='utf-8',
        )
        for name_status in output.strip().splitlines():
            status, name = name_status.split(maxsplit=1)
            if status != 'D':
                target_path = Path(root, name).resolve(strict=True)
                yield Target(target_path, target_path.read_text, passed_directly=path.is_file())


def _mine_targets(mine_opts: MineOptions) -> Generator[Target]:
    # read stdin if not tty
    if not mine_opts.tty:
        yield Target(PurePath(mine_opts.stdin_filename), sys.stdin.read, stdin=True)

    # read cwd if target is not specified
    if not mine_opts.targets and mine_opts.tty:
        paths = (Path.cwd(),)
    else:
        paths = mine_opts.targets

    if mine_opts.smart or mine_opts.smart_staged:
        yield from _mine_targets_smart(paths, mine_opts.smart_staged)
    else:
        yield from _mine_filepath_targets(paths)


def discover_style_targets(mine_opts: MineOptions) -> Generator[tuple[Target, set[type[stlr.Styler]]]]:
    for target in _mine_targets(mine_opts):
        state_helper.check_cancel_state()

        if styler_classes := stlr.select_suitable_stylers(
            target=target,
            file_types=mine_opts.file_types,
            enable_implicit_taxi_formatters=mine_opts.enable_implicit_taxi_formatters,
            paths_with_integrations=mine_opts.paths_with_integrations,
        ):
            yield target, styler_classes
