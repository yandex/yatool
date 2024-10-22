import coloredlogs
import concurrent.futures
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


def _discover_style_targets(args) -> dict[type[styler.BaseStyler], list[Target]]:
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


def _setup_logging(quiet: bool = False) -> None:
    console_log = logging.StreamHandler()

    while logging.root.hasHandlers():
        logging.root.removeHandler(logging.root.handlers[0])

    console_log.setLevel(logging.ERROR if quiet else logging.INFO)
    console_log.setFormatter(coloredlogs.ColoredFormatter('%(levelname).1s | %(message)s'))
    logging.root.addHandler(console_log)


def run_style(args) -> int:
    _setup_logging(args.quiet)

    style_targets = _discover_style_targets(args)

    rc = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.build_threads) as executor:
        futures = []
        for styler_class, target_loaders in style_targets.items():
            styler = styler_class(args)
            futures.extend(executor.submit(styler.style, *tl) for tl in target_loaders)
        for future in concurrent.futures.as_completed(futures):
            state_helper.check_cancel_state()
            rc = future.result() or rc

    return 3 if rc and args.check else 0
