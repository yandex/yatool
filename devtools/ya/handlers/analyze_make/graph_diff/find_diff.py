import json
import logging
import typing as tp
from collections.abc import Iterable, Generator
from pathlib import Path

import devtools.ya.build.graph_description as gd
import library.python.json as lpj  # type: ignore


logger = logging.getLogger(__name__)


BUILD_ROOT = "$(BUILD_ROOT)/"

type UidMap = dict[gd.GraphNodeUid, gd.GraphNode]


class FindDiffOptions(tp.NamedTuple):
    graph1: Path
    graph2: Path
    target_uids: tuple[gd.GraphNodeUid, gd.GraphNodeUid] | None = None
    target_output: str = ''


class NoSiblingError(Exception):
    mute = True


def load_graph(filename: Path) -> gd.DictGraph:
    with filename.open("rb") as afile:
        return lpj.loads(afile.read(), intern_keys=True, intern_vals=True)


def uid_map(graph: list[gd.GraphNode]) -> UidMap:
    return {entry["uid"]: entry for entry in graph}


def match_count(a: Iterable, b: Iterable) -> int:
    return len(set(a) & set(b))


def roots(uid_map: UidMap) -> set[gd.GraphNodeUid]:
    return {uid for uid, entry in uid_map.items() if not entry.get("deps")}


def find_first_diff(
    uid1: gd.GraphNodeUid,
    uid2: gd.GraphNodeUid,
    graph1: UidMap,
    graph2: UidMap,
    trace1: list[gd.GraphNodeUid],
    trace2: list[gd.GraphNodeUid],
) -> tuple[gd.GraphNodeUid, gd.GraphNodeUid]:
    assert uid1 != uid2, (uid1, uid2, (trace1, trace2))
    deps1 = set(graph1[uid1].get("deps", []))
    deps2 = set(graph2[uid2].get("deps", []))
    if deps1 == deps2:
        return uid1, uid2

    uniq1 = (deps1 - deps2).pop()
    cand2 = deps2 - deps1

    uniq2 = get_sibling(graph1[uniq1], cand2, graph2)
    trace1.append(uniq1)
    trace2.append(uniq2)

    return find_first_diff(uniq1, uniq2, graph1, graph2, trace1, trace2)


def get_sibling(target: gd.GraphNode, candidates: set[gd.GraphNodeUid], uid_map: UidMap) -> gd.GraphNodeUid:
    fields = {"outputs": 3, "inputs": 2, "tags": 1}
    field_sizes = {}
    for name in list(fields):
        if name not in target or not target.get(name):
            del fields[name]
        else:
            field_sizes[name] = len(target[name])
    assert fields, (target, candidates)
    uid_score = {}

    for uid in candidates:
        entry = uid_map[uid]
        total = 0
        for name, score in fields.items():
            if target[name] == entry.get(name):
                total += score
            else:
                diff = set(target[name]) ^ set(entry.get(name, []))
                total += (score - 1) + (field_sizes[name] - len(diff)) / field_sizes[name]

        if total:
            uid_score[uid] = total

    if not uid_score:
        raise NoSiblingError(
            "Failed to find sibling using {} for node: {}".format(
                fields.keys(),
                json.dumps({v: target[v] for v in list(fields.keys()) + ['uid']}, sort_keys=True, indent=4),
            )
        )
    return sorted(uid_score.items(), key=lambda x: x[1])[-1][0]


def strip_br(x: str) -> str:
    return x.removeprefix(BUILD_ROOT)


def nodes_by_output(graph: gd.DictGraph, output: str) -> Generator[gd.GraphNodeUid]:
    output = strip_br(output)
    for n in graph['graph']:
        for x in n['outputs']:
            if strip_br(x) == output:
                yield n['uid']


def find_diff(opts: FindDiffOptions) -> None:
    target_uids = opts.target_uids
    target_output = opts.target_output

    graph1 = load_graph(opts.graph1)
    graph2 = load_graph(opts.graph2)

    if target_uids:
        if target_uids[0] in graph1['result']:
            result1, result2 = [target_uids[0]], [target_uids[1]]
        elif target_uids[1] in graph1['result']:
            result2, result1 = [target_uids[0]], [target_uids[1]]
        else:
            logger.info("Warning: specified uids are not result uids")
            result1, result2 = [target_uids[0]], [target_uids[1]]

        logger.info("Nodes with output: %s|%s match:%s", len(result1), len(result2), match_count(result1, result2))
    elif target_output:
        result1 = list(set(nodes_by_output(graph1, target_output)))
        result2 = list(set(nodes_by_output(graph2, target_output)))
        logger.info(
            "Nodes with output %s: %s|%s match:%s",
            target_output,
            len(result1),
            len(result2),
            match_count(result1, result2),
        )
    else:
        result1: gd.GraphResult = graph1['result']
        result2: gd.GraphResult = graph2['result']
        logger.info("Result nodes %s|%s match:%s", len(result1), len(result2), match_count(result1, result2))

    graph1 = uid_map(graph1["graph"])
    graph2 = uid_map(graph2["graph"])
    logger.info("Nodes %s|%s match:%s", len(graph1), len(graph2), match_count(graph1, graph2))

    diff = set(result1) - set(result2)

    if not diff:
        logger.info('No diff in result nodes found')
        return

    res1, res2 = None, None
    while diff:
        res1 = diff.pop()
        try:
            res2 = get_sibling(graph1[res1], set(result2) - set(result1), graph2)
        except NoSiblingError:
            if not diff:
                raise
            continue
    logger.info("Results %s vs %s", res1, res2)

    assert res1 and res2

    trace1, trace2 = [res1], [res2]
    diff1, diff2 = find_first_diff(res1, res2, graph1, graph2, trace1, trace2)

    logger.info("First diff %s vs %s", diff1, diff2)
    logger.info("Deps %s", " -> ".join(trace1))
    logger.info("Deps %s", " -> ".join(trace2))
    logger.info("%s:\n%s\n", diff1, json.dumps(graph1[diff1], sort_keys=True, indent=4))
    logger.info("%s:\n%s", diff2, json.dumps(graph2[diff2], sort_keys=True, indent=4))
