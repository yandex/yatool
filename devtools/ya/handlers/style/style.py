import coloredlogs
import concurrent.futures
import logging

from . import state_helper
from . import styler
from . import target_miner


logger = logging.getLogger(__name__)


def _setup_logging(quiet: bool = False) -> None:
    console_log = logging.StreamHandler()

    while logging.root.hasHandlers():
        logging.root.removeHandler(logging.root.handlers[0])

    console_log.setLevel(logging.ERROR if quiet else logging.INFO)
    console_log.setFormatter(coloredlogs.ColoredFormatter('%(levelname).1s | %(message)s'))
    logging.root.addHandler(console_log)


def run_style(args) -> int:
    _setup_logging(args.quiet)

    style_targets = target_miner.discover_style_targets(args)

    rc = 0
    style_opts = styler.StyleOptions(
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
            futures.extend(executor.submit(styler.style, style_opts, styler_, *tl) for tl in target_loaders)
        for future in concurrent.futures.as_completed(futures):
            state_helper.check_cancel_state()
            rc = future.result() or rc

    return 3 if rc and style_opts.check else 0
