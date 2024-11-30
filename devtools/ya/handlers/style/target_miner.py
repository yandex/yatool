import logging
import os
import sys

from collections.abc import Generator, Callable
from pathlib import Path, PurePath

from . import state_helper
from . import styler


logger = logging.getLogger(__name__)

type Target = tuple[Path | PurePath, Callable[..., str]]


def _mine_targets(targets: list[str], stdin_filename: str) -> Generator[Target, None, None]:
    # always read stdin if not tty
    if not os.isatty(sys.stdin.fileno()):
        yield PurePath(styler.STDIN_FILENAME_STAMP + stdin_filename), sys.stdin.read

    # read cwd if target is not specified
    if not targets and os.isatty(sys.stdin.fileno()):
        targets.append(os.getcwd())

    for t in targets:
        target = Path(t)
        if target.is_file():
            yield target.absolute(), target.read_text
        elif target.is_dir():
            for dirpath, _, filenames in target.walk():
                for filename in filenames:
                    file = dirpath / filename
                    yield file.absolute(), file.read_text
        else:
            logger.warning('skip %s (no such file or directory)', target)


def discover_style_targets(args) -> dict[type[styler.Styler], list[Target]]:
    file_types = set(args.file_types)
    style_targets = {}
    for target, loader in _mine_targets(args.targets, args.stdin_filename):
        state_helper.check_cancel_state()
        styler_class = styler.select_styler(target=target, ruff=args.use_ruff)

        if not styler_class:
            logger.warning('skip %s (sufficient styler not found)', target)
            continue

        if file_types and styler_class.SPEC.kind not in file_types:
            logger.warning('skip %s (filtered by extensions)', target)
            continue

        if not file_types and not styler_class.DEFAULT_ENABLED:
            logger.warning('skip %s (require explicit --%s or --all)', target, styler_class.SPEC.kind)
            continue

        style_targets.setdefault(styler_class, []).append((target, loader))
    return style_targets
