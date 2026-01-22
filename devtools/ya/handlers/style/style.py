import difflib
import coloredlogs
import concurrent.futures
import itertools
import logging
import os
import sys
import typing as tp
from collections.abc import Callable, Generator
from pathlib import Path

import exts.os2
import yalibrary.display
import devtools.ya.handlers.style.config as cfg
import devtools.ya.handlers.style.disambiguate as disambiguate
import devtools.ya.handlers.style.state_helper as state_helper
import devtools.ya.handlers.style.styler as stlr
import devtools.ya.handlers.style.target as trgt
import devtools.ya.handlers.style.validate as vldt
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
        logging.root.handlers[0].close()
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


def _style(style_opts: StyleOptions, stylers: list[stlr.Styler], target: trgt.Target) -> int:
    """
    Execute `format` and store or display the result.
    Return 0 if no formatting happened, 1 otherwise
    """
    orig_content = content = target.reader()
    target_path = target.path

    if target.stdin:
        for styler in stylers:
            content = styler.format(target_path, content, target.stdin).content
        print(content, end='')
        return 0

    target_path = tp.cast(Path, target_path)  # could be PurePath only when from stdin
    if style_opts.force or not (reason := rules.get_skip_reason(str(target_path), content)):
        message = ""
        for styler in stylers:
            styler_output = styler.format(target_path, content, target.stdin)
            content = styler_output.content
            message += f"[[good]]{type(styler).__name__} styler fixed {target_path}[[rst]]"
            if styler_output.config:
                message += f" [[unimp]](config: {styler_output.config.pretty})[[rst]]"
            message += "\n"

        # TODO: check out cityhash comparison performance
        if content == orig_content:
            return 0

        if not style_opts.dry_run and style_opts.check:
            return 1

        if not style_opts.dry_run and not style_opts.check:
            display.emit_message(message)
            _flush_to_file(str(target_path), content)
        elif style_opts.dry_run:
            display.emit_message(message)
            _flush_to_terminal(orig_content, content, style_opts.full_output)
        return 1
    else:
        logger.warning("skip by rule: %s", reason)

    return 0


def _collect_target_stylers(
    mine_opts: trgt.MineOptions,
    disambiguation_opts: disambiguate.DisambiguationOptions,
) -> Generator[tuple[trgt.Target, list[type[stlr.Styler]], list[str]]]:
    key_fn: Callable[[type[stlr.Styler]], stlr.StylerKind] = lambda sc: sc.kind
    for target, styler_classes in trgt.discover_style_targets(mine_opts):
        target_stylers: list[type[stlr.Styler]] = []
        disamb_errors: list[str] = []
        for _, styler_group_by_kind in itertools.groupby(sorted(styler_classes, key=key_fn), key=key_fn):
            res = disambiguate.disambiguate_targets(target.path, set(styler_group_by_kind), disambiguation_opts)
            if isinstance(res, str):
                disamb_errors.append(res)
            else:
                target_stylers.append(res)
        yield target, target_stylers, disamb_errors


def run_style(args) -> int:
    _setup_logging(args.quiet)

    mine_opts = trgt.MineOptions(
        targets=tuple(Path(t) for t in args.targets),
        file_types=tuple(stlr.StylerKind(t) for t in args.file_types),
        stdin_filename=args.stdin_filename,
        enable_implicit_taxi_formatters=args.internal_enable_implicit_taxi_formatters,
        paths_with_integrations=tuple(args.internal_paths_with_integrations),
        smart=args.smart,
        smart_staged=args.smart_staged,
    )
    disambiguation_opts = disambiguate.DisambiguationOptions(
        use_ruff=args.use_ruff,
        use_clang_format_yt=args.use_clang_format_yt,
        use_clang_format_15=args.use_clang_format_15,
        use_clang_format_18_vanilla=args.use_clang_format_18_vanilla,
    )

    style_opts = StyleOptions(
        force=args.force,
        dry_run=args.dry_run,
        check=args.check,
        full_output=args.full_output,
    )
    styler_opts = stlr.StylerOptions(py2=args.py2)

    rc = 0
    disamb_errors: list[str] = []
    validation_configs: set[tuple[cfg.MaybeConfig, cfg.Config, Path]] = set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.build_threads) as executor:
        style_futures: list[concurrent.futures.Future[int]] = []
        for target, styler_classes, errors in _collect_target_stylers(mine_opts, disambiguation_opts):
            disamb_errors.extend(errors)
            stylers = [stlr.init_styler_cached(cls, styler_opts) for cls in styler_classes]
            style_futures.append(executor.submit(_style, style_opts, stylers, target))
            if args.validate:
                for styler in filter(stlr.is_configurable, stylers):
                    if configs := vldt.get_validation_configs(styler, target):
                        validation_configs.add(configs)

        for style_future in concurrent.futures.as_completed(style_futures):
            state_helper.check_cancel_state()
            try:
                rc = style_future.result() or rc
            except stlr.StylingError as e:
                logger.exception(e)
                return 1

    for configs in validation_configs:
        state_helper.check_cancel_state()
        if error := vldt.validate(*configs):
            logger.info(error)

    if disamb_errors:
        logger.warning(
            f"The following targets were not styled due to styler selection ambiguity. "
            f"Provide style option to disambiguate.\n"
            f"{'\n'.join(disamb_errors)}"
        )

    return 3 if rc and style_opts.check else 0
