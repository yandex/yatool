import difflib
import json
import logging
import typing as tp
from contextlib import ExitStack
from dataclasses import dataclass, asdict
from pathlib import Path

import devtools.ya.build.graph_description as gd
import library.python.json as lpj  # type: ignore


logger = logging.getLogger(__name__)


class InvalidNodeError(Exception):
    mute = True


class CompareOptions(tp.NamedTuple):
    graph1: Path
    graph2: Path
    dest_dir: Path


BENCHMARK_UNMATCHED_NODES_FILE_NAME = 'left-unmatched-nodes.txt'
TEST_UNMATCHED_NODES_FILE_NAME = 'right-unmatched-nodes.txt'
UID_ONLY_CHANGES_FILE_NAME = 'uid-only-changes.diff.txt'
SIGNIFICANT_BUT_NO_UID_CHANGES_FILE_NAME = 'significant-but-no-uid-changes.diff.txt'
SIGNIFICANT_CHANGES_FILE_NAME = 'significant-changes.diff.txt'
UID_AND_DEPS_CHANGES_ONLY_FILE_NAME = 'uid-and-deps-changes-only.diff.txt'
INSIGNIFICANT_CHANGES_FILE_NAME = 'insignificant-changes.diff.txt'

SIGNIFICANT_KEYS = {'uid', 'cmds', 'deps', 'outputs', 'env', 'platform', 'requirements', 'tags', 'target_properties'}


@dataclass
class CompareGraphStat:
    left_graph_node_count: int = 0
    right_graph_node_count: int = 0

    left_unmatched_node_count: int = 0
    right_unmatched_node_count: int = 0

    uid_only_changes_count: int = 0
    significant_but_no_uid_changes_count: int = 0
    significant_changes_count: int = 0
    uid_and_deps_only_changes_count: int = 0
    insignificant_changes_count: int = 0

    @property
    def fatal_error_count(self) -> int:
        return (
            self.left_unmatched_node_count
            + self.right_unmatched_node_count
            + self.uid_only_changes_count
            + self.significant_but_no_uid_changes_count
            + self.significant_changes_count
        )

    @property
    def total_error_count(self) -> int:
        return self.fatal_error_count + self.uid_and_deps_only_changes_count + self.insignificant_changes_count

    def as_dict(self) -> dict[str, tp.Any]:
        return asdict(self)


def compare_graphs(opts: CompareOptions) -> None:
    stat = CompareGraphStat()
    with ExitStack() as stack:
        left_graph = _load_and_preprocess_graph(opts.graph1)
        right_graph = _load_and_preprocess_graph(opts.graph2)

        left_unmatched_nodes_path = opts.dest_dir / BENCHMARK_UNMATCHED_NODES_FILE_NAME
        right_unmatched_nodes_path = opts.dest_dir / TEST_UNMATCHED_NODES_FILE_NAME
        uid_only_changes_path = opts.dest_dir / UID_ONLY_CHANGES_FILE_NAME
        significant_but_no_uid_changes_path = opts.dest_dir / SIGNIFICANT_BUT_NO_UID_CHANGES_FILE_NAME
        significant_changes_path = opts.dest_dir / SIGNIFICANT_CHANGES_FILE_NAME
        uid_and_deps_changes_only_path = opts.dest_dir / UID_AND_DEPS_CHANGES_ONLY_FILE_NAME
        insignificant_changes_path = opts.dest_dir / INSIGNIFICANT_CHANGES_FILE_NAME

        left_unmatched_nodes_file = stack.enter_context(open(left_unmatched_nodes_path, 'w'))
        right_unmatched_nodes_file = stack.enter_context(open(right_unmatched_nodes_path, 'w'))
        uid_only_changes_file = stack.enter_context(open(uid_only_changes_path, 'w'))
        significant_but_no_uid_changes_file = stack.enter_context(open(significant_but_no_uid_changes_path, 'w'))
        significant_changes_file = stack.enter_context(open(significant_changes_path, 'w'))
        uid_and_deps_changes_only_file = stack.enter_context(open(uid_and_deps_changes_only_path, 'w'))
        insignificant_changes_file = stack.enter_context(open(insignificant_changes_path, 'w'))

        stat.left_graph_node_count = len(left_graph.node_by_stats_uid)
        stat.right_graph_node_count = len(right_graph.node_by_stats_uid)

        differ = difflib.Differ()
        for right_stats_uid, right_node in right_graph.node_by_stats_uid.items():
            if right_stats_uid in left_graph.node_by_stats_uid:
                left_node = left_graph.node_by_stats_uid[right_stats_uid]
                diff_keys = _get_diff_keys(left_node, right_node)
                if diff_keys:
                    significant_diff_keys = diff_keys & SIGNIFICANT_KEYS
                    if significant_diff_keys:
                        if significant_diff_keys == {'uid'}:
                            diff_file = uid_only_changes_file
                            stat.uid_only_changes_count += 1
                        elif significant_diff_keys == {'uid', 'deps'}:
                            diff_file = uid_and_deps_changes_only_file
                            stat.uid_and_deps_only_changes_count += 1
                        elif 'uid' in significant_diff_keys:
                            diff_file = significant_changes_file
                            stat.significant_changes_count += 1
                        else:
                            diff_file = significant_but_no_uid_changes_file
                            stat.significant_but_no_uid_changes_count += 1
                    else:
                        diff_file = insignificant_changes_file
                        stat.insignificant_changes_count += 1

                    left_lines = _get_node_as_lines(left_node)
                    right_lines = _get_node_as_lines(right_node)
                    diff = differ.compare(left_lines, right_lines)
                    diff_file.writelines(diff)
            else:
                _dump_node(right_node, right_unmatched_nodes_file)
                stat.right_unmatched_node_count += 1

        # Dump left unmatched nodes
        if stat.left_graph_node_count > (stat.right_graph_node_count - stat.right_unmatched_node_count):
            for left_stats_uid, left_node in left_graph.node_by_stats_uid.items():
                if left_stats_uid not in right_graph.node_by_stats_uid:
                    _dump_node(left_node, left_unmatched_nodes_file)
                    stat.left_unmatched_node_count += 1

    logger.info("%s", json.dumps(stat.as_dict(), sort_keys=True, indent=4))


@dataclass
class Graph:
    full_graph: gd.DictGraph  # For future usage (compare context?)
    node_by_stats_uid: dict[gd.StatsUid, gd.GraphNode]


def _load_and_preprocess_graph(graph_path: Path) -> Graph:
    with graph_path.open('rb') as afile:
        graph = tp.cast(gd.DictGraph, lpj.loads(afile.read(), intern_keys=True, intern_vals=True))
        node_by_stats_uid: dict[gd.StatsUid, gd.GraphNode] = {}
        for node in graph['graph']:
            if node.keys() < {'stats_uid', 'uid', 'deps'}:
                raise InvalidNodeError(
                    f'Graph {graph_path} has a wrong node with missing one of required keys. The node: {node}'
                )
            # Don't care about inputs
            node.pop('inputs', None)
            # Sort arrays because order is unimportant
            for k in 'outputs', 'deps', 'tags':
                v = node.get(k)
                if v is not None and len(v) > 1:
                    v = sorted(v)
            # Remove insignificant env keys
            env = node.get('env', {})
            for k in list(env.keys()):
                if k.startswith('YA_'):
                    del env[k]
            node_by_stats_uid[node['stats_uid']] = node  # type: ignore
    return Graph(full_graph=graph, node_by_stats_uid=node_by_stats_uid)


def _dump_node(node: gd.GraphNode, f: tp.TextIO) -> None:
    lpj.dump(node, f, indent=4)
    f.write('\n')


def _get_node_as_lines(node: gd.GraphNode) -> list[str]:
    return (lpj.dumps(node, indent=4, sort_keys=True) + '\n').splitlines(keepends=True)


def _get_diff_keys(left: gd.GraphNode, right: gd.GraphNode) -> set[str]:
    result = set()
    if left != right:
        for k in set(left.keys()) | set(right.keys()):
            if left.get(k) != right.get(k):
                result.add(k)
    return result
