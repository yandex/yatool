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
    use_ruff: bool = False
    tty: bool = os.isatty(sys.stdin.fileno())


def _mine_targets(targets: tuple[Path, ...], mine_opts: MineOptions) -> Generator[Target, None, None]:
    # read stdin if not tty
    if not mine_opts.tty:
        yield PurePath(STDIN_FILENAME_STAMP + mine_opts.stdin_filename), sys.stdin.read

    # read cwd if target is not specified
    if not targets and mine_opts.tty:
        targets = (Path.cwd(),)

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


def discover_style_targets(mine_opts: MineOptions) -> dict[type[styler.Styler], list[Target]]:
    style_targets = {}
    for target, loader in _mine_targets(mine_opts.targets, mine_opts):
        state_helper.check_cancel_state()
        styler_class = styler.select_styler(target=target, mine_opts=mine_opts)

        if not styler_class:
            logger.warning('skip %s (sufficient styler not found)', target)
            continue

        if mine_opts.file_types and styler_class.kind not in mine_opts.file_types:
            logger.warning('skip %s (filtered by extensions)', target)
            continue

        if not mine_opts.file_types and not styler_class.default_enabled:
            logger.warning('skip %s (require explicit --%s or --all)', target, styler_class.kind)
            continue

        style_targets.setdefault(styler_class, []).append((target, loader))
    return style_targets
