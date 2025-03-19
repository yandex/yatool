import logging
from pathlib import Path

from . import compare
from . import find_diff


logger = logging.getLogger(__name__)


def _setup_logging():
    console_log = logging.StreamHandler()

    while logging.root.hasHandlers():
        logging.root.handlers[0].close()
        logging.root.removeHandler(logging.root.handlers[0])

    console_log.setLevel(logging.INFO)
    logging.root.addHandler(console_log)
    logging.basicConfig(level=logging.INFO)


def diff(args):
    _setup_logging()

    graph1, graph2 = Path(args.graphs[0]), Path(args.graphs[1])

    if args.find_diff_target_uids or args.find_diff_target_output:
        if args.find_diff_target_uids:
            diff_opts = find_diff.FindDiffOptions(
                graph1=graph1,
                graph2=graph2,
                target_uids=tuple(args.find_diff_target_uids),
            )
        else:
            diff_opts = find_diff.FindDiffOptions(
                graph1=graph1,
                graph2=graph2,
                target_output=args.find_diff_target_output,
            )
        find_diff.find_diff(diff_opts)
    else:
        compare_opts = compare.CompareOptions(
            graph1=graph1,
            graph2=graph2,
            dest_dir=Path(args.compare_dest_dir) if args.compare_dest_dir else Path.cwd(),
        )
        compare_opts.dest_dir.mkdir(parents=True, exist_ok=True)
        compare.compare_graphs(compare_opts)
