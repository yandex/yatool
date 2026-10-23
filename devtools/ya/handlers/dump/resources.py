import logging

import exts.yjson as json

import devtools.ya.app
import devtools.ya.build.build_opts
import devtools.ya.build.gen_plan2
from devtools.ya.core.yarg import ArgConsumer, Group, Options, SetConstValueHook
from yalibrary.fetcher import resource_fetcher

logger = logging.getLogger(__name__)

DUMP_RESOURCES_OPTS_GROUP = Group('Dump resources options', 1)


class DumpResourcesOptions(Options):
    def __init__(self):
        self.json_format = False
        self.all_platforms = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--json'],
                help='Dump resources in json format: {"conf": [...], "nodes": [...]} as they appear in the build graph',
                hook=SetConstValueHook('json_format', True),
                group=DUMP_RESOURCES_OPTS_GROUP,
            ),
            ArgConsumer(
                ['--all-platforms'],
                help='In plain format print resource URIs for all platforms instead of the host platform only',
                hook=SetConstValueHook('all_platforms', True),
                group=DUMP_RESOURCES_OPTS_GROUP,
            ),
        ]


DUMP_RESOURCES_OPTS = devtools.ya.build.build_opts.ya_make_options(free_build_targets=True) + [DumpResourcesOptions()]


def collect_graph_resources(graph: dict) -> dict:
    """Collect resources from a ya-bin build graph.

    Returns {"conf": [...], "nodes": [...]}: tool resources from graph['conf']['resources']
    deduplicated by pattern (first occurrence wins, same as in build/graph.py)
    and unique per-node resources (DATA(sbr://...) and alike) ordered by first use.
    """
    conf_resources = []
    seen_patterns = set()
    for item in graph.get('conf', {}).get('resources', []):
        pattern = item.get('pattern')
        if pattern in seen_patterns:
            continue
        seen_patterns.add(pattern)
        conf_resources.append(item)

    node_resources = []
    seen_nodes = set()
    for node in graph.get('graph', []):
        for item in node.get('resources', []):
            key = tuple(sorted(item.items()))
            if key in seen_nodes:
                continue
            seen_nodes.add(key)
            node_resources.append(item)

    return {'conf': conf_resources, 'nodes': node_resources}


def resource_uris(resources: dict, all_platforms: bool = False) -> list[str]:
    """Flatten collected resources into a sorted list of unique URIs.

    Multi-platform conf entries are resolved for the host platform unless all_platforms is set.
    Inline "base64:" entries (for example VCS info) are skipped: they are not external resources.
    """
    uris = set()
    for item in resources['conf']:
        if all_platforms and 'resources' in item:
            uris.update(res['resource'] for res in item['resources'])
        else:
            uris.add(resource_fetcher.select_resource(item)['resource'])
    for item in resources['nodes']:
        uris.add(item['uri'])
    return sorted(uri for uri in uris if not uri.startswith('base64:'))


def dump_resources(params) -> str:
    graph = devtools.ya.build.gen_plan2.ya_make_graph(params, devtools.ya.app, real_ya_make_opts=True)
    resources = collect_graph_resources(graph)

    if params.json_format:
        return json.dumps(resources, indent=4, sort_keys=True)
    return '\n'.join(resource_uris(resources, all_platforms=params.all_platforms))
