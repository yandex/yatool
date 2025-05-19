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


def _mine_targets(mine_opts: MineOptions) -> Generator[Target]:
    # read stdin if not tty
    if not mine_opts.tty:
        yield PurePath(STDIN_FILENAME_STAMP + mine_opts.stdin_filename), sys.stdin.read

    # read cwd if target is not specified
    if not mine_opts.targets and mine_opts.tty:
        targets = (Path.cwd(),)
    else:
        targets = mine_opts.targets

    for target in targets:
        if target.is_symlink():
            continue
        if target.is_file():
            yield target.absolute(), target.read_text
        elif target.is_dir():
            for dirpath, _, filenames in target.walk():
                for filename in filenames:
                    file = dirpath / filename
                    if file.is_symlink():
                        continue
                    yield file.absolute(), file.read_text
        else:
            logger.warning('skip %s (no such file or directory)', target)


def discover_style_targets(mine_opts: MineOptions) -> Generator[tuple[Target, set[type[styler.Styler]]]]:
    for target in _mine_targets(mine_opts):
        state_helper.check_cancel_state()

        if styler_classes := styler.select_suitable_stylers(target=target[0], file_types=mine_opts.file_types):
            yield target, styler_classes
