import logging
import os
import sys
import typing as tp

from collections.abc import Generator, Callable
from pathlib import Path, PurePath

from . import state_helper
from . import styler


STDIN_FILENAME = 'source.cpp'
STDIN_FILENAME_STAMP = 'STDIN_FILENAME_STAMP'

logger = logging.getLogger(__name__)

type Target = tuple[Path | PurePath, Callable[..., str]]


class MineOptions(tp.NamedTuple):
    targets: tuple[Path, ...]
    file_types: tuple[styler.StylerKind, ...] = ()
    stdin_filename: str = STDIN_FILENAME
    tty: bool = os.isatty(sys.stdin.fileno())


def _mine_filepath_targets(paths: tuple[Path]) -> Generator[Target]:
    for path in paths:
        if path.is_symlink():
            continue
        if path.is_file():
            yield path.resolve(), path.read_text
        elif path.is_dir():
            path = path.resolve()
            for dirpath, _, filenames in path.walk():
                if dirpath.is_symlink():
                    continue

                for filename in filenames:
                    filepath = dirpath / filename
                    if filepath.is_symlink():
                        continue
                    yield filepath, filepath.read_text
        else:
            logger.warning('skip %s (no such file or directory)', path)


def _mine_targets(mine_opts: MineOptions) -> Generator[Target]:
    # read stdin if not tty
    if not mine_opts.tty:
        yield PurePath(STDIN_FILENAME_STAMP + mine_opts.stdin_filename), sys.stdin.read

    # read cwd if target is not specified
    if not mine_opts.targets and mine_opts.tty:
        paths = (Path.cwd(),)
    else:
        paths = mine_opts.targets

    yield from _mine_filepath_targets(paths)


def discover_style_targets(mine_opts: MineOptions) -> Generator[tuple[Target, set[type[styler.Styler]]]]:
    for target in _mine_targets(mine_opts):
        state_helper.check_cancel_state()

        if styler_classes := styler.select_suitable_stylers(target=target[0], file_types=mine_opts.file_types):
            yield target, styler_classes
