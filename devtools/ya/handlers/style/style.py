import difflib
import coloredlogs
import concurrent.futures
import itertools
import logging
import os
import sys
import typing as tp
from collections.abc import Callable
from pathlib import Path

import exts.os2
import yalibrary.display
from . import config as cfg
from . import disambiguate
from . import state_helper
from . import styler as stlr
from . import target as trgt
from library.python.testing.style import rules
from library.python.fs import replace_file


logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


class StyleOptions(tp.NamedTuple):
    force: bool = False
    dry_run: bool = False
    check: bool = False
    full_output: bool = False


class StyleOutput(tp.NamedTuple):
    rc: int
    styler: stlr.Styler | None = None
    config: cfg.MaybeConfig = None


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


def _style(style_opts: StyleOptions, styler: stlr.Styler, target: trgt.Target) -> StyleOutput:
    """
    Execute `format` and store or display the result.
    Return 0 if no formatting happened, 1 otherwise
    """
    content = target.reader()
    target_path = target.path

    if target.stdin:
        print(styler.format(target_path, content, target.stdin).content)
        return StyleOutput(rc=0, styler=styler)

    target_path = tp.cast(Path, target_path)  # could be PurePath only when from stdin
    if style_opts.force or not (reason := rules.get_skip_reason(str(target_path), content)):
        styler_output = styler.format(target_path, content, target.stdin)
        if styler_output.content == content:
            return StyleOutput(rc=0, styler=styler, config=styler_output.config)

        if not style_opts.dry_run and style_opts.check:
            return StyleOutput(rc=1, styler=styler, config=styler_output.config)

        message = f"[[good]]{type(styler).__name__} styler fixed {target_path}[[rst]]"
        if styler_output.config:
            message += f" [[unimp]](config: {styler_output.config.pretty})[[rst]]"

        if not style_opts.dry_run and not style_opts.check:
            display.emit_message(message)
            _flush_to_file(str(target_path), styler_output.content)
        elif style_opts.dry_run:
            display.emit_message(message)
            _flush_to_terminal(content, styler_output.content, style_opts.full_output)
        return StyleOutput(rc=1, styler=styler, config=styler_output.config)
    else:
        logger.warning("skip by rule: %s", reason)

    return StyleOutput(rc=0)


def _collect_style_targets(
    mine_opts: trgt.MineOptions,
    disambiguation_opts: disambiguate.DisambiguationOptions,
) -> tuple[dict[type[stlr.Styler], list[trgt.Target]], list[str]]:
    style_targets: dict[type[stlr.Styler], list[trgt.Target]] = {}
    disamb_errors: list[str] = []
    key_fn: Callable[[type[stlr.Styler]], stlr.StylerKind] = lambda sc: sc.kind
    for target, styler_classes in trgt.discover_style_targets(mine_opts):
        for _, styler_group_by_kind in itertools.groupby(sorted(styler_classes, key=key_fn), key=key_fn):
            res = disambiguate.disambiguate_targets(target.path, set(styler_group_by_kind), disambiguation_opts)
            if isinstance(res, str):
                disamb_errors.append(res)
            else:
                style_targets.setdefault(res, []).append(target)
    return style_targets, disamb_errors


def run_style(args) -> int:
    _setup_logging(args.quiet)

    mine_opts = trgt.MineOptions(
        targets=tuple(Path(t) for t in args.targets),
        file_types=tuple(stlr.StylerKind(t) for t in args.file_types),
        stdin_filename=args.stdin_filename,
        enable_implicit_taxi_formatters=args.internal_enable_implicit_taxi_formatters,
    )
    disambiguation_opts = disambiguate.DisambiguationOptions(
        use_ruff=args.use_ruff,
        use_clang_format_yt=args.use_clang_format_yt,
        use_clang_format_15=args.use_clang_format_15,
        use_clang_format_18_vanilla=args.use_clang_format_18_vanilla,
    )
    style_targets, disamb_errors = _collect_style_targets(mine_opts, disambiguation_opts)

    style_opts = StyleOptions(
        force=args.force,
        dry_run=args.dry_run,
        check=args.check,
        full_output=args.full_output,
    )
    styler_opts = stlr.StylerOptions(py2=args.py2)

    # We may have:
    # 1. Multiple stylers using the same user config;
    # 2. A single styler using different user configs for different targets.
    user_configs: dict[cfg.ConfigPath, set[stlr.ConfigurableStyler]] = {}

    rc = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.build_threads) as executor:
        futures: list[concurrent.futures.Future[StyleOutput]] = []

        for styler_class, targets in style_targets.items():
            styler_ = styler_class(styler_opts)
            futures.extend(executor.submit(_style, style_opts, styler_, target) for target in targets)

        for future in concurrent.futures.as_completed(futures):
            state_helper.check_cancel_state()
            try:
                out = future.result()
            except stlr.StylingError as e:
                logger.error(e, exc_info=True)
                return 1

            rc = out.rc or rc

            # save user defined configs for validation
            if (
                args.validate
                and out.styler
                and stlr.is_configurable(out.styler)
                and out.config
                and out.config.user_defined
            ):
                user_configs.setdefault(out.config.path, set()).add(out.styler)

    for config_path, stylers in user_configs.items():
        for styler, rules_config, errors in cfg.validate(config_path, stylers):
            logger.info(
                'Config validation failed for config %s, styler %s, rules %s',
                config_path,
                type(styler).__name__,
                rules_config,
            )
            logger.info('\n'.join(errors) + '\n')

    if disamb_errors:
        logger.warning(
            f"The following targets were not styled due to styler selection ambiguity. "
            f"Provide style option to disambiguate.\n"
            f"{'\n'.join(disamb_errors)}"
        )

    return 3 if rc and style_opts.check else 0
