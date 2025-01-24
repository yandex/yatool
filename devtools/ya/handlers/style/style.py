import difflib
import coloredlogs
import concurrent.futures
import logging
import os
import sys
import typing as tp
from pathlib import Path

import exts.os2
import yalibrary.display
from . import state_helper
from . import styler
from . import target
from library.python.testing.style import rules
from library.python.fs import replace_file


logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


class StyleOptions(tp.NamedTuple):
    force: bool = False
    dry_run: bool = False
    check: bool = False
    full_output: bool = False


def _setup_logging(quiet: bool = False) -> None:
    console_log = logging.StreamHandler()

    while logging.root.hasHandlers():
        logging.root.removeHandler(logging.root.handlers[0])

    console_log.setLevel(logging.ERROR if quiet else logging.INFO)
    console_log.setFormatter(coloredlogs.ColoredFormatter('%(levelname).1s | %(message)s'))
    logging.root.addHandler(console_log)


def _flush_to_file(path: str, content: str) -> None:
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(content.encode())

    # never break original file
    path_st_mode = os.stat(path).st_mode
    replace_file(tmp, path)
    os.chmod(path, path_st_mode)


def _flush_to_terminal(content: str, formatted_content: str, full_output: bool) -> None:
    if full_output:
        display.emit_message(formatted_content)
    else:
        diff = difflib.unified_diff(content.splitlines(), formatted_content.splitlines())
        diff = list(diff)[2:]  # Drop header with filenames
        diff = "\n".join(diff)

        display.emit_message(diff)


def _style(style_opts: StyleOptions, styler: styler.Styler, target_: target.Target) -> tp.Literal[0, 1]:
    """
    Execute `format` and store or display the result.
    Return 0 if no formatting happened, 1 otherwise
    """
    target_path, loader = target_
    content = loader()

    if target_path.name.startswith(target.STDIN_FILENAME_STAMP):
        print(styler.format(target_path, content).content)
        return 0

    target_path = tp.cast(Path, target_path)
    if style_opts.force or not (reason := rules.get_skip_reason(str(target_path), content)):
        styler_output = styler.format(target_path, content)
        if styler_output.content == content:
            return 0

        if not style_opts.dry_run and style_opts.check:
            return 1

        message = f"[[good]]{type(styler).__name__} styler fixed {target_path}[[rst]]"
        if styler_output.config:
            message += f" [[unimp]](config: {styler_output.config.pretty})[[rst]]"

        if not style_opts.dry_run and not style_opts.check:
            display.emit_message(message)
            _flush_to_file(str(target_path), styler_output.content)
        elif style_opts.dry_run:
            display.emit_message(message)
            _flush_to_terminal(content, styler_output.content, style_opts.full_output)
        return 1
    else:
        logger.warning("skip by rule: %s", reason)

    return 0


def run_style(args) -> int:
    _setup_logging(args.quiet)

    mine_opts = target.MineOptions(
        targets=tuple(Path(t) for t in args.targets),
        file_types=tuple(styler.StylerKind(t) for t in args.file_types),
        stdin_filename=args.stdin_filename,
        use_ruff=args.use_ruff,
    )

    style_targets = target.discover_style_targets(mine_opts)

    rc = 0
    style_opts = StyleOptions(
        force=args.force,
        dry_run=args.dry_run,
        check=args.check,
        full_output=args.full_output,
    )
    styler_opts = styler.StylerOptions(py2=args.py2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.build_threads) as executor:
        futures = []
        for styler_class, target_loaders in style_targets.items():
            styler_ = styler_class(styler_opts)
            futures.extend(executor.submit(_style, style_opts, styler_, tl) for tl in target_loaders)
        for future in concurrent.futures.as_completed(futures):
            state_helper.check_cancel_state()
            try:
                rc = future.result() or rc
            except styler.StylingError as e:
                logger.error(e, exc_info=True)
                return 1

    return 3 if rc and style_opts.check else 0
