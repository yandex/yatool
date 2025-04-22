import base64
import collections
from concurrent.futures import ThreadPoolExecutor
import contextlib2
import copy
from enum import Enum
import logging
import os
import re2
import six
import queue
import sys
import tempfile
import devtools.ya.test.const as test_consts
import traceback

import typing as tp  # noqa
from itertools import chain

import exts.fs
import exts.func
import exts.path2
import exts.asyncthread as core_async
import exts.timer
import exts.yjson as json
import exts.hashing as hashing
import exts.func
from exts.strtobool import strtobool
import exts.uniq_id

import app_config
import yalibrary.fetcher.tool_chain_fetcher as fetcher
import yalibrary.tools as tools
import devtools.ya.core.yarg
import devtools.ya.core.report
import devtools.ya.core.event_handling
from devtools.ya.core.imprint import imprint
from devtools.ya.core import stage_tracer
import devtools.ya.test.const as tconst

import yalibrary.platform_matcher as pm
import yalibrary.graph.commands as graph_cmd
import yalibrary.graph.const as graph_const
import yalibrary.graph.node as graph_node
from devtools.ya.yalibrary.yandex.distbuild import distbs_consts
from yalibrary.toolscache import toolscache_version
import yalibrary.vcs.vcsversion as vcsversion
import yalibrary.debug_store
from yalibrary.monitoring import YaMonEvent

import devtools.ya.build.makelist as bml
import devtools.ya.build.node_checks as node_checks
import devtools.ya.build.gen_plan as gen_plan
import devtools.ya.build.ymake2 as ymake2
import devtools.ya.build.graph_description as graph_descr
from devtools.ya.build.ymake2.consts import YmakeEvents
import devtools.ya.build.genconf as bg
from devtools.ya.build.evlog.progress import get_print_status_func
import devtools.ya.build.ccgraph as ccgraph

if tp.TYPE_CHECKING:
    from devtools.ya.test.test_types.common import AbstractTestSuite


import devtools.libs.yaplatform.python.platform_map as platform_map

try:
    import yalibrary.build_graph_cache as bg_cache
except ImportError:

    class Mock:
        def configure_build_graph_cache_dir(*args, **kwargs):
            return None

        def archive_cache_dir(*args, **kwargs):
            return None

        def BuildGraphCacheCLFromArc(*args, **kwargs):
            return None

    bg_cache = Mock()


ALLOWED_EXTRA_RESOURCES = {
    'JDK': 'java',
    'JDK10': 'java10',
    'MAVEN': 'mvn',
    'ERRORPRONE': 'error_prone',
    'JSTYLERUNNER': 'jstyle_runner',
    'KYTHE': 'kythe',
    'KYTHETOPROTO': 'kythe_to_proto',
    'SONAR_SCANNER': 'sonar_scanner',
    'UBERJAR': 'uber_jar',
    'UBERJAR_10': 'uber_jar10',
    'SCRIPTGEN': 'scriptgen',
    'PYTHON3': 'python3',
    'GDB': 'gdb',
    'DLV': 'dlv',
    'YTEXEC': 'ytexec',
    'KTLINT': 'ktlint',
    'KTLINT_OLD': 'ktlint_old',
}

GRAPH_STAT_VERSION = 1
EVENTS_WITH_PROGRESS = YmakeEvents.DEFAULT.value + YmakeEvents.PROGRESS.value


logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("graph")


class GraphBuildError(Exception):
    retriable = False


class _OptimizableGraph(dict):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.resource_pattern = re2.compile(r'\$\((.*?)\)')
        self.actual_resources_patterns = {'VCS'}

    def filter_graph_resources(self, resources, list_of_commands):
        for string in list_of_commands:
            matches = self.resource_pattern.findall(string)
            if matches:
                self.actual_resources_patterns.update(matches)

        result = [r for r in resources if r['pattern'] in self.actual_resources_patterns]

        return result

    def optimize_resources(self):
        reduce_graph_resources_stage = stager.start('reduce_graph_resources')
        try:
            resources = self['conf']['resources']
            commands = set()
            for item in self.get('graph', []):
                item_env = item.get('env', {})
                if item_env:
                    commands.update(item_env.values())

                for cmd in item.get('cmds', []):
                    commands.update(cmd.get('cmd_args', []))
                    cmd_env = cmd.get('env', {})

                    if cmd_env:
                        commands.update(cmd_env.values())

            if resources:
                filtered_resources = self.filter_graph_resources(resources, commands)
                self['conf']['resources'] = filtered_resources
        except Exception as err:
            logger.error(err, exc_info=True)
        finally:
            reduce_graph_resources_stage.finish()


class _NodeGen:
    def __init__(self):
        self.extra_nodes: list[graph_descr.GraphNode] = []
        self._md5_cache = {}

    def resolve_file_md5(self, path):
        if path not in self._md5_cache:
            self._md5_cache[path] = hashing.md5_path(path)

        return self._md5_cache[path]

    @staticmethod
    def is_cacheable_node(node):
        return node.get('cache', not node.get('kv', {}).get('disable_cache', False))

    def gen_rename_node(self, node: graph_descr.GraphNode, suffix) -> graph_descr.GraphNodeUid:
        import devtools.ya.test.test_node.cmdline as cmdline

        inputs = node['outputs']
        outputs = [inp + suffix for inp in inputs]

        tared_outputs = [inp + suffix for inp in node.get('tared_outputs', [])]
        cmd_args = sum([[fr, to] for fr, to in zip(inputs, outputs)], [])

        ret = {
            'uid': hashing.md5_value(node['uid'] + '-' + suffix),
            'broadcast': False,
            'cmds': [
                {
                    'cmd_args': ["$(PYTHON)/python", '$(SOURCE_ROOT)/build/scripts/move.py']
                    + cmdline.wrap_with_cmd_file_markers(cmd_args)
                }
            ],
            'deps': [node['uid']],
            'inputs': inputs,
            'kv': {'p': 'CP', 'pc': 'light-blue'},
            'outputs': outputs,
            'priority': 0,
            'env': {},
            'cache': _NodeGen.is_cacheable_node(node),
            "type": 2,
        }
        if tared_outputs:
            ret['tared_outputs'] = tared_outputs
        self.extra_nodes.append(self.copy_tags(node, ret))
        return ret['uid']

    def copy_tags(self, fr, to):
        tags = fr.get('tags', None)
        if tags:
            to['tags'] = tags
        return to

    def gen_and_save_graph_node(self, uid, deps, inputs, outputs, cmd, tags):
        ret = {
            'uid': uid,
            'deps': deps,
            'inputs': inputs,
            'outputs': outputs,
            'priority': 0,
            'cmds': [{'cmd_args': cmd}],
            'kv': {'p': 'UB', 'pc': 'light-magenta'},
            'cwd': '$(BUILD_ROOT)',
            'env': {},
            'cache': True,
            'type': 2,
        }

        if tags:
            ret['tags'] = tags

        self.extra_nodes.append(ret)

        return ret['uid']


def union_inputs(inputs1, inputs2):
    inputs1.update(inputs2)
    return inputs1


def _iter_cmds(args):
    if 'cmd_args' in args and args['cmd_args']:
        res = {}

        for x in (
            'cmd_args',
            'cwd',
            'env',
            'stdout',
        ):
            if x in args:
                res[x] = args[x]

        if res:
            yield res

    if 'cmds' in args:
        yield from args['cmds']


def _merge_nodes(x, y):
    ret = {
        'uid': x['uid'],
        'deps': y['deps'],
        'inputs': y['inputs'],
        'outputs': x['outputs'] + y['outputs'],
        'cmds': list(_iter_cmds(y)) + list(_iter_cmds(x)),
    }

    for k in ('cache', 'broadcast', 'target_properties', 'platform', 'priority', 'kv'):
        if k in x:
            ret[k] = x[k]

    ret['tags'] = [y.get('kv', {}).get('p', 'unknown').lower()] + x.get('tags', []) + y.get('tags', [])

    return ret


def _optimize_graph(graph):
    by_uid = {}

    for x in graph['graph']:
        by_uid[x['uid']] = x

    r_deps = collections.defaultdict(list)

    for uid, node in by_uid.items():
        for d in node['deps']:
            r_deps[d].append(uid)

    def iter_nodes():
        visited = set()

        def visit(x):
            uid = x['uid']

            if uid not in visited:
                visited.add(uid)

                while len(x['deps']) == 1 and len(r_deps[x['deps'][0]]) == 1:
                    x = _merge_nodes(x, by_uid[x['deps'][0]])

                yield x

                for dep in x['deps']:
                    yield from visit(by_uid[dep])

        for n in graph['result']:
            yield from visit(by_uid[n])

    return {
        'inputs': graph.get('inputs', {}),
        'result': graph['result'],
        'graph': list(iter_nodes()),
        'conf': graph['conf'],
    }


def _add_modules_to_results(graph):
    results = set(graph['result'])
    for node in graph['graph']:
        if node_checks.is_module(node):
            results.add(node['uid'])
    graph['result'] = list(results)
    return graph


def _set_share_for_results(graph):
    results = set(graph['result'])
    for node in graph['graph']:
        if node['uid'] in results:
            node['share'] = True
    return graph


def _add_json_prefix(graph, tests, prefix):
    old_to_new_uids = {}
    for node in graph['graph']:
        old_to_new_uids[node['uid']] = prefix + node['uid']

    _substitute_uids(graph, tests, old_to_new_uids)


def _gen_rename_nodes(graph: graph_descr.DictGraph, uid_map, src_dir):
    node_gen = _NodeGen()
    by_output = collections.defaultdict(list)

    for uid in graph['result']:
        n = uid_map[uid]

        for out in n['outputs']:
            by_output[out].append(n)

    def iter_results():
        uids_to_rename = set()
        for out, lst in by_output.items():
            if len(lst) > 1:
                uids_to_rename |= {n['uid'] for n in lst}
            else:
                yield lst[0]['uid']

        for u in uids_to_rename:
            node = uid_map[u]

            # We would like to preserve resaonably-sized prefixes, but trim longer ones
            prefix = '-'.join(node.get('tags', [u]))
            if len(prefix) > 64:
                prefix = '{}..{}'.format(prefix[:54], hashing.md5_value(prefix)[:8])

            yield node_gen.gen_rename_node(node, '.' + prefix)

    graph['result'] = [x for x in iter_results()]
    graph['graph'].extend(node_gen.extra_nodes)
    return graph


def _iter_extra_resources(g):
    if node_checks.is_empty_graph(g):
        return

    seen = set()

    for n in g['graph']:
        for k in n.get('kv', {}):
            if k.startswith('needs_resource'):
                k = k[len('needs_resource') :]
                if k in ALLOWED_EXTRA_RESOURCES and k not in seen:
                    seen.add(k)
                    yield k, ALLOWED_EXTRA_RESOURCES[k]


def _naive_merge(g1: graph_descr.DictGraph | None, g2: graph_descr.DictGraph | None) -> graph_descr.DictGraph:
    assert g1 is not None or g2 is not None

    if node_checks.is_empty_graph(g1):
        return g2

    if node_checks.is_empty_graph(g2):
        return g1

    g1: graph_descr.DictGraph
    g2: graph_descr.DictGraph

    conf1 = g1.get('conf', {}).copy()
    conf2 = g2.get('conf', {})

    conf1['resources'] = conf1.get('resources', []) + conf2.get('resources', [])

    return {
        'conf': conf1,
        'graph': g1['graph'] + g2['graph'],
        'result': g1['result'] + g2['result'],
        'inputs': _union_inputs_from_graphs(g1, g2),
    }


def _union_inputs_from_graphs(g1, g2):
    return union_inputs(g1.get('inputs', {}), g2.get('inputs', {}))


def _add_resources(resources, to):
    to_resources = to.get('conf', {}).get('resources', [])
    patterns = {r['pattern'] for r in to_resources}
    for r in resources:
        if r['pattern'] not in patterns:
            to_resources.append(r)
    to.setdefault('conf', {})['resources'] = to_resources
    return to


def strip_graph(
    graph: graph_descr.DictGraph, result: tp.Sequence[graph_descr.GraphNodeUid] | None = None
) -> graph_descr.DictGraph:
    result = result or graph['result']
    nodes = _strip_unused_nodes(graph['graph'], result)

    conf = graph.get('conf', {}).copy()
    conf['resources'] = _filter_duplicate_resources(conf.get('resources', []))

    return {'conf': conf, 'inputs': graph.get('inputs', {}), 'result': list(set(result)), 'graph': nodes}


def _strip_unused_nodes(
    graph_nodes: list[graph_descr.GraphNode], result: tp.Sequence[graph_descr.GraphNodeUid]
) -> list[graph_descr.GraphNode]:
    by_uid = {n['uid']: n for n in graph_nodes}

    def visit(uid):
        if uid in by_uid:
            node = by_uid.pop(uid)
            yield node
            for dep in node['deps']:
                yield from visit(dep)

    result_nodes: list[graph_descr.GraphNode] = []
    for uid in result:
        for node in visit(uid):
            result_nodes.append(node)

    logger.debug('stripped %d, left %d nodes', len(graph_nodes) - len(result_nodes), len(result_nodes))

    return result_nodes


def _filter_duplicate_resources(resources):
    v = set()
    result = []
    for x in resources:
        if x['pattern'] not in v:
            v.add(x['pattern'])
            result.append(x)
    return result


class GraphMalformedException(Exception):
    pass


_TargetGraphsResult = collections.namedtuple("TargetGraphsResult", ["pic", "no_pic", "target_tc"])

# class _TargetGraphsResult(tp.NamedTuple):
#     pic


# _MergeTargetGraphResult = collections.namedtuple("_MergeTargetGraphResult", ["graph", "test_bundle", "make_files"])
class _MergeTargetGraphResult(tp.NamedTuple):
    graph: ccgraph.Graph | graph_descr.DictGraph
    test_bundle: tp.Any
    make_files: tp.Any


# _GenGraphResult = collections.namedtuple("_GenGraphResult", ["graph", "tc_tests", "java_darts", "make_files_map"])


class _GenGraphResult(tp.NamedTuple):
    graph: ccgraph.Graph
    tc_tests: list["AbstractTestSuite"]
    java_darts: list
    make_files_map: list


def _get_node_out_names_map(node):
    KV_PRE = 'ext_out_name_for_'
    return {k[len(KV_PRE) :]: v for k, v in node.get('kv').items() if k.startswith(KV_PRE)}


def _apply_out_names_map(mapping, name):
    dirname, basename = os.path.split(name)
    new_name = mapping.get(basename)
    if new_name:
        return os.path.join(dirname, new_name), True
    else:
        return name, False


def filter_nodes_by_output(graph, flt, warn=False, host=None, any_match=False):
    found = False

    for node in graph.get('graph'):
        mapping = _get_node_out_names_map(node)
        has_match = False
        has_mismatch = False
        has_renaming = False
        if host is not None:
            if host != node.get('host_platform', False):
                continue
        for out_name in node.get('outputs'):
            (new_out_name, renamed) = _apply_out_names_map(mapping, out_name)
            if new_out_name.endswith(flt):
                has_match = True
            elif not any_match:
                has_mismatch = True
            has_renaming = has_renaming or renamed
        if has_match:
            found = True
            full_match = not has_mismatch and not has_renaming
            yield node, full_match

    if warn and not found:
        logger.warning('No nodes found for output: {}'.format(flt))


def _resolve_tool_resid(tool, res_dir):
    formula = tool['formula']
    host_info = tool['platform']['host']
    platform = host_info['os'].lower()
    arch = host_info['arch'].lower()

    if tool['name'] == 'msvc2013':
        platform = 'win32'

    if tool['name'].startswith('msvc2015'):  # Dancing on the crutches...
        platform = 'win32'

    if tool['name'] == 'gcc49' and platform == 'cygwin':  # XXX: resource platform should be multiple
        platform = 'win32'

    try:
        platform = pm.canonize_platform(platform)
    except pm.PlatformNotSupportedException:
        if platform == 'freebsd':
            platform = 'freebsd9'
        else:
            raise

    if arch != 'x86_64':
        platform = platform + '-' + arch

    return fetcher.resolve_resource_id(res_dir, tool['name'], tool['bottle_name'], formula, platform)


def _resolve_tool(tool: tools.ToolInfo, res_dir) -> graph_descr.GraphConfResourceConcreteInfo:
    if tool['params'].get('use_bundle', False):
        formula = fetcher.get_formula_value(tool['formula'])
        return platform_map.graph_json_from_resource_json(tool['params']['match_root'], json.dumps(formula))
    else:
        return {
            'resource': str(_resolve_tool_resid(tool, res_dir)),
            'pattern': tool['params']['match_root'],
            'name': tool.get('name', 'nameless tool'),
        }


def _extend_build_type(build_type):
    if build_type.startswith('dist-'):
        return build_type
    return 'dist-' + build_type


def _split_gcc_node(n):
    pp = n.copy()
    cc = n.copy()

    cmd_args = n['cmd_args']
    common_args = cmd_args[7:]

    out = cmd_args[5]  # noqa
    inp = cmd_args[6]
    exe = cmd_args[2]

    cpp_out = os.path.dirname(inp).replace('SOURCE_ROOT', 'BUILD_ROOT') + '/pp_' + os.path.basename(inp)

    cpp_cmd = [exe, '-E', inp] + common_args

    pp['kv'] = pp['kv'].copy()
    pp['kv']['p'] = 'PP'
    pp['cmd_args'] = cpp_cmd
    pp['outputs'] = [cpp_out]
    pp['stdout'] = cpp_out
    pp['uid'] += '_pp'

    ccc_cmd = cmd_args[:6] + [cpp_out] + common_args

    filtered_cmds = []
    for x in ccc_cmd:
        if not (x.startswith('-D') or x.startswith('-I')):
            filtered_cmds.append(x)

    cc['cmd_args'] = filtered_cmds
    cc['inputs'] = [cpp_out]
    cc['deps'] = [pp['uid']]

    yield pp
    yield cc


def _split_gcc(g):
    for n in g:
        if n.get('kv', {}).get('p', '') == 'CC':
            yield from _split_gcc_node(n)
        else:
            yield n


def _substitute_uids(graph, tests, replaces, propagate=False):
    timer = exts.timer.Timer('substitude_uids')

    if propagate:
        nodes = {}

        for n in graph['graph']:
            nodes[n['uid']] = n

        seen = {}

        def traverse(uid):
            if uid in seen:
                return seen[uid]

            deps = nodes[uid].get('deps', ())
            affected = uid in replaces

            if affected:
                newdeps = deps[:]
            else:
                newdeps = None

            for i, x in enumerate(deps):
                if traverse(x):
                    affected = True
                    # materialize new deps only in case of processing affected node
                    if newdeps is None:
                        newdeps = deps[:]
                    newdeps[i] = replaces[x]

            if affected:
                if uid not in replaces:
                    if tconst.UID_PREFIX_DELIMITER in uid:
                        prefix = uid[: uid.rfind(tconst.UID_PREFIX_DELIMITER) + 1]
                    else:
                        prefix = ''
                    replaces[uid] = prefix + hashing.md5_value(" ".join([uid] + newdeps))
                nodes[uid]['deps'] = newdeps

            seen[uid] = affected
            return affected

        for x in graph['result']:
            traverse(x)

        for x in replaces:
            n = nodes[x]
            n['uid'] = replaces[x]

        timer.show_step("propagation")
    else:
        for node in graph['graph']:
            if node['uid'] in replaces:
                node['uid'] = replaces[node['uid']]
            node['deps'] = [replaces.get(dep_uid, dep_uid) for dep_uid in node['deps']]

    graph['result'] = [replaces.get(res_uid, res_uid) for res_uid in graph['result']]
    if 'context' in graph['conf'] and 'sandbox_run_test_result_uids' in graph['conf']['context']:
        graph['conf']['context']['sandbox_run_test_result_uids'] = [
            replaces.get(res_uid, res_uid) for res_uid in graph['conf']['context']['sandbox_run_test_result_uids']
        ]

    for test in tests:
        test.uid = replaces.get(test.uid, test.uid)
        test._result_uids = [replaces.get(dep_uid, dep_uid) for dep_uid in test._result_uids]
        test._output_uids = [replaces.get(dep_uid, dep_uid) for dep_uid in test._output_uids]
        test.dep_uids = [replaces.get(dep_uid, dep_uid) for dep_uid in test.dep_uids]
        test.change_build_dep_uids(replaces)

    timer.show_step('substitude_uids')


def _add_requirements(node, reqs):
    if not reqs:
        return False
    if 'requirements' not in node:
        node['requirements'] = {}
    modified = False
    for req in reqs:
        if req not in node['requirements']:
            node['requirements'][req] = reqs[req]
            modified = True
    return modified


def _inject_default_requirements(graph, tests, reqs):
    old_to_new_uids = {}
    for node in graph['graph']:
        is_modified = _add_requirements(node, reqs)
        if is_modified:
            old_to_new_uids[node['uid']] = hashing.md5_value(str([sorted(node['requirements'].items()), node['uid']]))

    logger.debug('Updated requirements for %d nodes', len(old_to_new_uids))
    _substitute_uids(graph, tests, old_to_new_uids)


def _propagate_cache_false_from_kv(graph):
    for n in graph['graph']:
        if n.get('kv', {}).get('disable_cache'):
            n['cache'] = False


# This function removes all common tags (tags which present in all nodes)
# and assign the only tag "tool" to the host (tool) nodes.
# The former is to reduce length of a runner progress report (runner adds all tags to a report line)
# and the latter is somehow used in distbuild.
#
# TODO: do the common tag removing in the runner before start (not in graph generating like this).
# TODO: assign tool tag in _build_tools().
def _strip_tags(nodes):
    by_tag = collections.defaultdict(int)
    node_count = 0

    for node in nodes:
        if node_checks.is_host_platform(node):
            node['tags'] = ['tool']
        else:
            node_count += 1
            tags = node.get('tags')
            if tags:
                for t in tags:
                    by_tag[t] += 1

    bad_tags = frozenset(k for k, v in by_tag.items() if v == node_count)
    if bad_tags:
        for node in nodes:
            if not node_checks.is_host_platform(node):
                tags = node.get('tags')
                if tags:
                    node['tags'] = [tag for tag in tags if tag not in bad_tags]

    return nodes


# See build_graph_and_tests::iter_target_flags
def _remap_graph(graph, mapper):
    for node in graph.get('graph', []):
        node['uid'] = mapper(node['uid'])
        for i, dep_uid in enumerate(node.get('deps', [])):
            node['deps'][i] = mapper(dep_uid)
    for i, result_uid in enumerate(graph['result']):
        graph['result'][i] = mapper(result_uid)


def _transform_nodes_uids(graph, transformer):
    nodes_map, rehash_map = {}, {}
    for node in graph.get('graph', []):
        rehash_map[node['uid']] = transformer(node)
        nodes_map[node['uid']] = node

    dfs_map = {}

    def _dfs(uid):
        if dfs_map.get(uid, False) is None:
            raise Exception('Cycle detected in graph')
        if uid not in dfs_map:
            dfs_map[uid] = None
            new_uid = rehash_map[uid]
            for dep_uid in (_dfs(dep) for dep in sorted(nodes_map[uid].get('deps', []))):
                new_uid = hashing.fast_hash(new_uid + dep_uid)
            dfs_map[uid] = new_uid
        return dfs_map[uid]

    for uid in graph.get('result', []):
        _dfs(uid)
    return _remap_graph(graph, lambda x: dfs_map.get(x, x))


def _transformer_for_pgo(pgo_hash):
    def _transform_node(node):
        for cmd in node.get('cmds', []):
            for cmd_arg in cmd.get('cmd_args', []):
                if '$(PGO_PATH)' in cmd_arg:
                    return hashing.fast_hash(node['uid'] + pgo_hash)
        return node['uid']

    return _transform_node


def _add_pgo_profile_resource(graph, pgo_path):
    graph['conf']['resources'].append(
        {
            'name': 'pgo_profile',
            'pattern': 'PGO_PATH',
            'resource': 'file:' + pgo_path,
        },
    )
    return hashing.fast_filehash(os.path.abspath(pgo_path))


def _gen_filter_node(node: graph_descr.GraphNode, flt) -> graph_descr.GraphNode:
    mapping = _get_node_out_names_map(node)

    inputs = []
    cmds = []
    outputs = []
    for inp in node.get('outputs'):
        outp, renamed = _apply_out_names_map(mapping, inp)
        if not outp.endswith(flt):
            continue
        inputs.append(inp)
        if renamed:
            cmd = {
                'cmd_args': [
                    "$(PYTHON)/python",
                    '$(SOURCE_ROOT)/build/scripts/fs_tools.py',
                    'link_or_copy',
                    inp,
                    outp,
                ]
            }
            cmds.append(cmd)
        outputs.append(outp)

    uid = 'output_filter:' + hashing.md5_value(str([node['uid'], flt]))

    return {
        'uid': uid,
        'deps': [node['uid']],
        'inputs': inputs,
        'outputs': outputs,
        'priority': 0,
        'cmds': cmds,
        'kv': node.get('kv', {}),
        'cwd': '$(BUILD_ROOT)',
        'env': {},
        'cache': True,
        'type': 2,
    }


def finalize_graph(graph: graph_descr.DictGraph, opts):
    if opts.add_result or opts.add_host_result:
        assert 'result' in graph

        def iter_filter_nodes(filters, host=False):
            for flt in filters:
                for node, full_match in filter_nodes_by_output(
                    graph, flt, warn=False, host=host, any_match=opts.all_outputs_to_result
                ):
                    if node.get('uid') in graph['result']:
                        continue
                    if full_match:
                        yield node, False
                    else:
                        yield _gen_filter_node(node, flt), True

        if opts.replace_result:
            graph['result'] = []

        for node, need_to_add in chain(
            list(iter_filter_nodes(opts.add_result, host=False)),
            list(iter_filter_nodes(opts.add_host_result, host=True)),
        ):
            if need_to_add:
                graph.get('graph').append(node)
            graph.get('result').append(node.get('uid'))

        if opts.replace_result:
            graph = strip_graph(graph)

    if opts.upload_to_remote_store and opts.download_artifacts and 'result' in graph:
        graph_result = set(graph['result'])  # TODO YA-316
        for node in graph['graph']:
            if node.get('uid') in graph_result:
                node['upload'] = True


def _load_stat(graph_stat_path):
    load_graph_stat_stage = stager.start("load_graph_stat")

    try:
        with open(graph_stat_path) as f:
            return json.load(f)
    except Exception as e:
        logger.exception("Can not load statistics: %r", e)
        try:
            devtools.ya.core.report.telemetry.report(
                devtools.ya.core.report.ReportTypes.WTF_ERRORS, {"graph.load_stat": traceback.format_exc()}
            )
        except Exception as ee:
            logger.exception("Can not report error: %r", ee)
    finally:
        load_graph_stat_stage.finish()


def _add_stat_to_graph(graph, stat, path_filters=None):
    add_stat_to_graph_stage = stager.start("add_stat_to_graph")

    stat_version = (stat or {}).get("version")
    if stat_version != GRAPH_STAT_VERSION:
        logger.error(
            "Unable to load graph statistics: Graph statistics version %s, expected version is %s",
            stat_version,
            GRAPH_STAT_VERSION,
        )
        add_stat_to_graph_stage.finish()
        return

    path_filters = ["$(BUILD_ROOT)/{}".format(pf) for pf in (path_filters or [])]

    min_reqs_errors = 0
    for node in graph["graph"]:
        try:
            if not _node_matches_filter(node, path_filters):
                continue

            node_stat = stat["by_stats_uid"].get(node["stats_uid"])
            if node.get("kv") and node.get("kv").get("p"):
                node_stat = node_stat or stat["by_node_type"].get(node["kv"]["p"], {}).get(
                    _get_full_platform_name(node.get('platform'), node.get('tags'))
                )
            if node_stat and "skip" not in node_stat:
                node["min_reqs"] = node_stat
                _update_graph_execution_cost(node_stat, graph["conf"]["execution_cost"])

        except Exception as e:
            _handle_error("Can not apply statistics: %r", e)
            node["min_reqs_error"] = 1
            min_reqs_errors += 1

    graph["conf"]["min_reqs_errors"] = min_reqs_errors

    add_stat_to_graph_stage.finish()


def _get_full_platform_name(platform, tags):
    return "-".join([platform or ""] + sorted(tags or []))


def _handle_error(*msg):
    logger.exception(*msg)
    try:
        devtools.ya.core.report.telemetry.report(
            devtools.ya.core.report.ReportTypes.WTF_ERRORS, {"graph.add_stat_to_graph": traceback.format_exc()}
        )
    except Exception as e:
        logger.exception("Can not report error: %r", e)


def _node_matches_filter(n, path_filters):
    if not path_filters:
        return True

    for o in n["outputs"]:
        for path_filter in path_filters:
            if o.startswith(path_filter):
                return True

    return False


def _update_graph_execution_cost(stat, cost_info):
    from devtools.libs.parse_number.python import parse_number as pn

    cpu = cost_info.get("cpu", 0)
    try:
        # parse_human_readable_number does not expect concrete unit to be specified, only its' prefix needed.
        # but duration is in format 123.45ms, so we trim "s" standing for "seconds"
        cost_info["cpu"] = cpu + pn.parse_human_readable_number(stat["cpu"]) * (
            pn.parse_human_readable_number(stat["duration"][:-1]) if "duration" in stat else 0
        )
    except Exception as e:
        _handle_error("Can not get execution cost for node stat %s: %r", stat, e)
        cost_info["evaluation_errors"] = cost_info.get("evaluation_errors", 0) + 1


def _prepare_local_change_list_generator(opts):
    if not getattr(opts, "build_graph_cache_force_local_cl", False):
        return None

    try:
        import app_ctx
    except ImportError:
        return None

    if app_ctx.vcs_type != 'arc':
        return None

    if bg_cache:
        evlog = getattr(app_ctx, "evlog", None)
        store = getattr(app_ctx, "changelist_store", None)
        return bg_cache.BuildGraphCacheCLFromArc(evlog, opts, store=store)


class _AsyncContext:
    __slots__ = ('_future', '_done')

    def __init__(self, future):
        self._future = future
        self._done = False

    def __enter__(self):
        return self

    def __exit__(self, *exc_details):
        if self._done:
            return False

        res = core_async.wrap(self._future)
        logger.debug("Running future at __exit__: %s, unwinding %s", str(res)[:200], exc_details)
        self._done = True

        return False

    def __call__(self):
        try:
            return self._future()
        finally:
            self._done = True


class _ToolTargetsQueue:
    def __init__(self):
        self._queue = queue.Queue()
        self._sources_ids = {}
        self.__app_ctx = sys.modules.get("app_ctx")
        self._logger = logging.getLogger(self.__class__.__name__)

    def add_source(self, func, debug_id):
        source_id = len(self._sources_ids)
        self._sources_ids[source_id] = debug_id

        def _putter(val, async_result=None):
            self._queue.put((source_id, val, async_result))

        def _wrapper(*args, **kwargs):
            async_result = core_async.wrap(func, *args, **kwargs)
            #  Notify getter about thread termination (normal or with error)
            _putter(None, async_result)
            return core_async.unwrap(async_result)

        return _wrapper, _putter

    def done(self):
        return not self._sources_ids

    def get(self):
        targets = set()
        while self._sources_ids:
            (source_id, res, async_result) = self._interruptable_queue_get()
            # Receives either res or async_result
            assert bool(res is None) != bool(async_result is None)
            if res is not None:
                assert source_id in self._sources_ids, "Unknown or already received source_id={}".format(source_id)
                self._logger.debug(
                    "Source_id={} ({}). Tool targets: {}".format(source_id, self._sources_ids[source_id], res)
                )
                targets |= res
                del self._sources_ids[source_id]
            elif async_result is not None and source_id in self._sources_ids:
                core_async.unwrap(async_result)  # does nothing or raises thread error
                raise RuntimeError("Thread has terminated before tool targets sending")
        return targets

    def _interruptable_queue_get(self):
        if not self.__app_ctx:
            return self._queue.get()
        while True:
            try:
                return self._queue.get(timeout=1)
            except queue.Empty:
                self.__app_ctx.state.check_cancel_state()


class _ToolEventsQueueServerMode(_ToolTargetsQueue):
    def __init__(self):
        super().__init__()
        self.__bypasses_received = 0
        self.__finals_received = set()

    def done(self):
        return self._queue.qsize() == 0 and len(self.__finals_received) == len(self._sources_ids)

    def get(self):
        (source_id, res, async_result) = self._interruptable_queue_get()
        assert bool(res is None) != bool(async_result is None)
        if res is not None:
            assert source_id in self._sources_ids, "Unknown source_id={}".format(source_id)
            self._logger.debug("Source_id={} ({}). Event: {}".format(source_id, self._sources_ids[source_id], res))
            typename = res["_typename"]
            if typename == "NEvent.TAllForeignPlatformsReported":
                self.__finals_received.add(source_id)
                if len(self.__finals_received) < len(self._sources_ids):
                    res = {}  # report AllForeignPlatformsReported only for last source (prone to races though)
            elif typename == "NEvent.TBypassConfigure":
                if self.__bypasses_received == len(self._sources_ids):
                    res = {}  # skip others if we already had False
                elif res["Enabled"] is False:
                    self.__bypasses_received = len(self._sources_ids)
                else:
                    self.__bypasses_received += 1
                    if self.__bypasses_received < len(self._sources_ids):
                        res = {}  # report BypassConfigure only for last source when all was True
        elif async_result is not None:
            if source_id in self.__finals_received:
                res = {}
            else:
                try:
                    core_async.unwrap(async_result)  # does nothing or raises thread error
                except Exception:
                    # The thread is terminated with an error and hasn't sent TAllForeignPlatformsReported.
                    # So we must count thread termination as a final event too.
                    self.__finals_received.add(source_id)
                else:
                    # The thread is terminated w/o errors but hasn't sent TAllForeignPlatformsReported.
                    # This should not happen, let's tell the user about that.
                    raise RuntimeError("Thread has terminated before sending TAllForeignPlatformsReported")
        return res


def should_use_servermode_for_tools(opts):
    return opts.ymake_tool_servermode


def create_tool_event_queue(opts):
    if should_use_servermode_for_tools(opts):
        return _ToolEventsQueueServerMode()
    else:
        return _ToolTargetsQueue()


def should_use_servermode_for_pic(opts):
    return opts.ymake_pic_servermode


class _GraphKind(Enum):
    TARGET = 0
    TOOLS = 1
    GLOBAL_TOOLS = 2


class _ForeignEventListener:
    TOOL_PLATFORM = 0
    PIC_PLATFORM = 1

    def __init__(self, ev_listener, queue_putter):
        self.__prev_ev_listener = ev_listener
        self._queue_putter = queue_putter
        self.__done = False

    def _process_target_event(self, _):
        pass

    def _process_final_event(self, _):
        pass

    def __call__(self, event):
        typename = event["_typename"]
        if typename == "NEvent.TForeignPlatformTarget":
            if self.__done:
                logger.warning("NEvent.TForeignPlatformTarget event comes after NEvent.TAllForeignPlatformsReported")
            else:
                self._process_target_event(event)
        elif typename == "NEvent.TAllForeignPlatformsReported":
            if self.__done:
                logger.warning("Duplicate NEvent.TAllForeignPlatformsReported event")
            else:
                self._process_final_event(event)
                self.__done = True
        self.__prev_ev_listener(event)


class _ToolEventListener(_ForeignEventListener):
    def __init__(self, ev_listener, queue_putter):
        super().__init__(ev_listener, queue_putter)
        self.__tool_targets = set()

    def _process_target_event(self, event):
        if event["Reachable"] == 1 and event["Platform"] == self.TOOL_PLATFORM:
            self.__tool_targets.add(event["Dir"])

    def _process_final_event(self, _):
        self._queue_putter(self.__tool_targets)


class _ForeignEventListenerServerMode(_ForeignEventListener):
    def __init__(self, ev_listener, queue_putter):
        super().__init__(ev_listener, queue_putter)

    def _process_target_event(self, event):
        self._queue_putter(event)

    def _process_final_event(self, event):
        self._queue_putter(event)

    def _process_bypass_event(self, event):
        self._queue_putter(event)

    def __call__(self, event):
        super().__call__(event)
        if event["_typename"] == "NEvent.TBypassConfigure":
            self._process_bypass_event(event)


class _ToolEventListenerServerMode(_ForeignEventListenerServerMode):
    def __init__(self, ev_listener, queue_putter):
        super().__init__(ev_listener, queue_putter)

    def _process_target_event(self, event):
        if event["Platform"] == self.TOOL_PLATFORM:
            super()._process_target_event(event)


def create_tool_event_listener(opts, *args):
    if should_use_servermode_for_tools(opts):
        return _ToolEventListenerServerMode(*args)
    else:
        return _ToolEventListener(*args)


class _PicEventListenerServerMode(_ForeignEventListenerServerMode):
    def __init__(self, ev_listener, queue_putter):
        super().__init__(ev_listener, queue_putter)

    def _process_target_event(self, event):
        if event["Platform"] == self.PIC_PLATFORM:
            super()._process_target_event(event)


def build_graph_and_tests(opts, check, event_queue=None, display=None):
    if event_queue is None:
        try:
            import app_ctx

            event_queue = app_ctx.event_queue
        except ImportError:
            event_queue = devtools.ya.core.event_handling.EventQueue()

    with contextlib2.ExitStack() as exit_stack:
        try:
            return _build_graph_and_tests(opts, check, event_queue, exit_stack, display)
        except AssertionError:
            ex_type, ex_val, ex_bt = sys.exc_info()
            raise GraphBuildError("{} from {} exception".format(ex_val, ex_type)).with_traceback(ex_bt)


def _clang_tidy_strip_deps(plan, suites):
    def get_diff(lhs, rhs):
        return list(sorted(set(lhs) - set(rhs)))

    seen_uids = {}
    for suite in suites:
        if type(suite).__name__ != "ClangTidySuite":
            continue

        for out in suite.clang_tidy_inputs:
            uid = plan.get_uids_by_outputs(out)
            if not uid:
                continue

            node = plan.get_node_by_uid(uid[0])
            uids_to_remove = set()
            inputs_to_remove = set()
            for dep in node["deps"]:
                if dep in seen_uids:
                    inputs = seen_uids[dep]
                else:
                    dep_node = plan.get_node_by_uid(dep)
                    if dep_node["kv"].get("p") != "CC":
                        inputs = dep_node["outputs"]
                    else:
                        inputs = []
                    seen_uids[dep] = inputs
                if inputs:
                    inputs_to_remove.update(inputs)
                    uids_to_remove.add(dep)

            node["inputs"] = get_diff(node["inputs"], inputs_to_remove)
            node["deps"] = get_diff(node["deps"], uids_to_remove)


def _shorten_debug_id(debug_id, limit=64):
    if len(debug_id) <= limit:
        return debug_id
    # Aligns length to the nearest separator ("-")
    delim_pos = debug_id.rfind("-", 0, limit + 1)
    if delim_pos < 0 or delim_pos == limit:
        return debug_id[:limit]
    else:
        return debug_id[:delim_pos]


class _ConfErrorReporter:
    def __init__(self, ev_listener):
        self.__ev_listener = ev_listener

    def __call__(self, msg, path, sub='YaConf'):
        event = {
            'Message': msg,
            'Mod': 'bad',
            'Sub': sub,
            'Type': 'Error',
            'Where': path,
            '_typename': 'NEvent.TDisplayMessage',
        }
        self.__ev_listener(event)


class _GraphMaker:
    def __init__(
        self,
        opts,
        ymake_bin,
        real_ymake_bin,
        src_dir,
        bld_dir,
        res_dir,
        event_queue,
        test_target_platforms,
        check,
        exit_stack,
        print_status,
        cl_generator,
    ):
        self._opts = opts
        self._platform_threadpool = ThreadPoolExecutor(
            max_workers=getattr(opts, 'ya_threads')
        )  # must be unbound when ymake is multithreaded
        self._ymake_bin = ymake_bin
        self._real_ymake_bin = real_ymake_bin
        self._src_dir = src_dir
        self._conf_dir = os.path.join(bld_dir, 'conf')
        self._res_dir = res_dir
        self._event_queue = event_queue
        self._test_target_platforms = test_target_platforms
        self._check = check
        self._exit_stack = exit_stack
        self._print_status = print_status
        self._heater = self._opts.build_graph_cache_heater
        self._allow_changelist = True
        self._cl_generator = cl_generator
        self.ymakes_scheduled = 0

    def disable_changelist(self):
        self._allow_changelist = False

    def make_graphs(
        self,
        target_tc,
        abs_targets=None,
        graph_kind=_GraphKind.TARGET,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        extra_conf=None,
        tool_targets_queue=None,
        ymake_opts=None,
    ):
        is_cross_tools = graph_kind == _GraphKind.TOOLS or graph_kind == _GraphKind.GLOBAL_TOOLS
        is_global_tools = graph_kind == _GraphKind.GLOBAL_TOOLS

        if is_cross_tools:
            flags = self._opts.host_flags.copy()
            flags.update(self._opts.host_platform_flags)
            flags['IS_CROSS_TOOLS'] = 'yes'
        else:
            flags = self._opts.flags.copy()

        cache_dir = bg_cache.configure_build_graph_cache_dir(self._opts)
        if cache_dir:
            if node_checks.is_tools_tc(target_tc):
                cache_dir = self._build_graph_cache_dirname(target_tc, cache_dir, 'TOOLS')
            elif is_global_tools:
                cache_dir = self._build_graph_cache_dirname(target_tc, cache_dir, 'GTOOLS')
            else:
                cache_dir = self._build_graph_cache_dirname(target_tc, cache_dir)

        self._print_status("Configuring dependencies for platform {}".format(self._make_debug_id(debug_id, None)))

        # Choose what platform to build
        to_build_pic = False
        to_build_no_pic = False
        if self._non_pic_only(target_tc):
            to_build_no_pic = True
        elif self._opts.force_pic or self._pic_only(flags, target_tc):
            to_build_pic = True
        elif graph_kind == _GraphKind.TOOLS:
            to_build_pic = True
        elif graph_kind == _GraphKind.GLOBAL_TOOLS:
            to_build_no_pic = True
        else:
            to_build_pic = True
            to_build_no_pic = True

        if to_build_pic and not to_build_no_pic:
            logger.debug('Configuring only PIC for %s', self._make_debug_id(debug_id, None))
        elif to_build_no_pic and not to_build_pic:
            logger.debug('Configuring only non-PIC for %s', self._make_debug_id(debug_id, None))

        assert to_build_pic or to_build_no_pic

        pic = None
        no_pic = None
        pic_queue = None
        # The order of async tasks here is crucial when pic runs in servermode:
        # nopic MUST be scheduled before pic in case when both are presented.
        # This guarantees no deadlocks when there are more tasks than threads.
        if to_build_no_pic:
            no_pic_func = self._build_no_pic
            no_pic_tool_queue_putter = None
            no_pic_queue_putter = None
            if tool_targets_queue:
                no_pic_func, no_pic_tool_queue_putter = tool_targets_queue.add_source(
                    no_pic_func, self._make_debug_id(debug_id, 'nopic')
                )
            ymake_opts_nopic = ymake_opts
            if to_build_pic and should_use_servermode_for_pic(self._opts):
                pic_queue = _ToolEventsQueueServerMode()
                no_pic_func, no_pic_queue_putter = pic_queue.add_source(
                    no_pic_func, self._make_debug_id(debug_id, 'nopic')
                )

                ymake_opts_nopic = dict(
                    ymake_opts or {},
                    transition_source='nopic',
                    report_pic_nopic=True,
                )

            ymake_opts_nopic = dict(
                ymake_opts_nopic or {},
                order=self.ymakes_scheduled,
            )
            self.ymakes_scheduled += 1

            def gen_no_pic():
                return no_pic_func(
                    flags,
                    target_tc,
                    abs_targets,
                    debug_id=debug_id,
                    enabled_events=enabled_events,
                    extra_conf=extra_conf,
                    cache_dir=cache_dir,
                    change_list=self._opts.build_graph_cache_cl,
                    no_caches_on_retry=self._opts.no_caches_on_retry,
                    no_ymake_retry=self._opts.no_ymake_retry,
                    tool_targets_queue_putter=no_pic_tool_queue_putter,
                    pic_queue_putter=no_pic_queue_putter,
                    ymake_opts=ymake_opts_nopic,
                )

            no_pic = self._exit_stack.enter_context(_AsyncContext(self._platform_threadpool.submit(gen_no_pic).result))

        # The order of async tasks here is crucial when pic runs in servermode:
        # nopic MUST be scheduled before pic in case when both are presented.
        # This guarantees no deadlocks when there are more tasks than threads.
        if to_build_pic:
            pic_func = self._build_pic
            pic_queue_putter = None
            if tool_targets_queue:
                pic_func, pic_queue_putter = tool_targets_queue.add_source(
                    pic_func, self._make_debug_id(debug_id, 'pic')
                )

            ymake_opts_pic = ymake_opts
            abs_targets_pic = abs_targets
            if pic_queue is not None:

                def stdin_line_provider():
                    # This is a workaround. TShellCommand pulls stdin anytime the child process tries to write to any of std{out,err}.
                    # So we must not block here if we know there wont be any new events in the queue.
                    # Returning an empty string effectively closes the stream.
                    # TODO: we should throw an exception here instead to make the user responsible of closing the stream.
                    if pic_queue.done():
                        return ''
                    return json.dumps(pic_queue.get()) + '\n'

                ymake_opts_pic = dict(
                    ymake_opts or {},
                    targets_from_evlog=True,
                    source_root=self._opts.arc_root,
                    stdin_line_provider=stdin_line_provider,
                    transition_source='pic',
                    dont_check_transitive_requirements=True,  # FIXME YMAKE-1612: fix transitive checks in servermode and remove this flag
                )
                abs_targets_pic = []

            ymake_opts_pic = dict(
                ymake_opts_pic or {},
                order=self.ymakes_scheduled,
            )
            self.ymakes_scheduled += 1

            def gen_pic():
                return pic_func(
                    flags,
                    target_tc,
                    abs_targets_pic,
                    debug_id=debug_id,
                    enabled_events=enabled_events,
                    extra_conf=extra_conf,
                    cache_dir=cache_dir,
                    change_list=self._opts.build_graph_cache_cl,
                    no_caches_on_retry=self._opts.no_caches_on_retry,
                    no_ymake_retry=self._opts.no_ymake_retry,
                    tool_targets_queue_putter=pic_queue_putter,
                    ymake_opts=ymake_opts_pic,
                )

            pic = self._exit_stack.enter_context(_AsyncContext(self._platform_threadpool.submit(gen_pic).result))

        # For compatibility with merge_graph() and all other similar code
        # Will be removed somewhen
        if pic is None:
            pic = no_pic
        elif no_pic is None:
            no_pic = pic

        return _TargetGraphsResult(pic, no_pic, target_tc)

    @staticmethod
    def _build_graph_cache_dirname(target_tc_orig, cache_dir_root, platform_name=None):
        target_tc = copy.deepcopy(target_tc_orig)
        flags = target_tc.get('flags', {})
        for i in ['RECURSE_PARTITIONS_COUNT', 'RECURSE_PARTITION_INDEX']:
            if i in flags:
                del flags[i]

        target_tc.pop('targets', None)
        target_tc.pop('executable_path', None)
        target_tc.pop('tool_var', None)

        json_str = json.dumps(target_tc, sort_keys=True)
        base_name = ""
        if platform_name:
            base_name += platform_name
        elif 'platform_name' in target_tc:
            base_name += target_tc['platform_name']

        base_name += "-" + hashing.md5_value(json_str)
        cache_dir = os.path.join(cache_dir_root, base_name)
        logger.debug(
            "Creating ymake directory for target_tc: {}".format(json.dumps(target_tc, sort_keys=True, indent=2))
        )
        if not os.path.exists(cache_dir):
            exts.fs.ensure_dir(cache_dir)
            logger.debug("Created ymake directory")

        target_json = os.path.join(cache_dir, "target.json")
        if not os.path.exists(target_json):
            logger.debug("Wrote target.json into ymake directory")
            with open(target_json, 'w') as f:
                json.dump(target_tc, f, sort_keys=True, indent=2)

        logger.debug("Done with ymake directory")
        return cache_dir

    @staticmethod
    def _non_pic_only(target_tc):
        # Windows builds are always relocatable, but not PIC
        return target_tc['platform']['target']['os'] == "WIN"

    @staticmethod
    def _pic_only(flags, target_tc):
        # Honor -DPIC and --target-platlform-flag=PIC as force_pic
        return strtobool(flags.get('PIC', 'no')) or strtobool(target_tc.get('flags', {}).get('PIC', 'no'))

    def _build_pic(
        self,
        flags,
        target_tc,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
        tool_targets_queue_putter=None,
        ymake_opts=None,
    ):
        return self._prepare_graph(
            flags,
            target_tc,
            'pic',
            abs_targets,
            debug_id=self._make_debug_id(debug_id, 'pic'),
            enabled_events=enabled_events,
            cache_dir=cache_dir,
            change_list=change_list,
            extra_conf=extra_conf,
            no_caches_on_retry=no_caches_on_retry,
            no_ymake_retry=no_ymake_retry,
            tool_targets_queue_putter=tool_targets_queue_putter,
            ymake_opts=ymake_opts,
        )

    def _build_no_pic(
        self,
        flags,
        target_tc,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
        tool_targets_queue_putter=None,
        pic_queue_putter=None,
        ymake_opts=None,
    ):
        flags = copy.deepcopy(flags)
        flags['FORCE_NO_PIC'] = 'yes'
        return self._prepare_graph(
            flags,
            target_tc,
            None,
            abs_targets,
            debug_id=self._make_debug_id(debug_id, 'nopic'),
            enabled_events=enabled_events,
            cache_dir=cache_dir,
            change_list=change_list,
            extra_conf=extra_conf,
            no_caches_on_retry=no_caches_on_retry,
            no_ymake_retry=no_ymake_retry,
            tool_targets_queue_putter=tool_targets_queue_putter,
            pic_queue_putter=pic_queue_putter,
            ymake_opts=ymake_opts,
        )

    def _prepare_graph(
        self,
        flags,
        target_tc,
        extra_tag,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
        tool_targets_queue_putter=None,
        pic_queue_putter=None,
        ymake_opts=None,
    ):
        cache_subdir = None
        if cache_dir:
            cache_subdir = cache_dir if extra_tag is None else os.path.join(cache_dir, extra_tag)
            exts.fs.ensure_dir(cache_subdir)
        tags, platform = gen_plan.prepare_tags(target_tc, flags, self._opts)
        with stager.scope("gen-graph-{}".format(_shorten_debug_id(debug_id))):
            result = self._gen_graph(
                flags,
                target_tc,
                abs_targets,
                debug_id=debug_id,
                enabled_events=enabled_events,
                extra_conf=extra_conf,
                cache_dir=cache_subdir,
                change_list=change_list,
                no_caches_on_retry=no_caches_on_retry,
                no_ymake_retry=no_ymake_retry,
                tool_targets_queue_putter=tool_targets_queue_putter,
                pic_queue_putter=pic_queue_putter,
                ymake_opts=ymake_opts,
            )
        result.graph.set_tags(tags + ([extra_tag] if extra_tag else []))
        result.graph.set_platform(platform)
        return result

    def _gen_graph(
        self,
        flags,
        tc,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
        tool_targets_queue_putter=None,
        pic_queue_putter=None,
        ymake_opts=None,
    ) -> _GenGraphResult:
        flags = copy.deepcopy(flags)
        flags['IS_CROSS_SANITIZE'] = 'yes'
        tc_tests = []
        should_run_tc_tests = _should_run_tests(self._opts, tc)

        tmp_dir = tempfile.mkdtemp(prefix='gen_graph.')

        test_dart_path = None
        if should_run_tc_tests:
            test_dart_path = os.path.join(tmp_dir, 'test.dart')

        java_dart_path = None
        if flags.get('YA_IDE_IDEA', 'no') == 'yes':
            java_dart_path = os.path.join(tmp_dir, 'java.dart')
        make_files_dart_path = os.path.join(tmp_dir, 'makefiles.dart')

        current_ev_listener = self._event_queue
        if tool_targets_queue_putter is not None:
            current_ev_listener = create_tool_event_listener(self._opts, self._event_queue, tool_targets_queue_putter)
            enabled_events += YmakeEvents.TOOLS.value
        if pic_queue_putter is not None:
            current_ev_listener = _PicEventListenerServerMode(current_ev_listener, pic_queue_putter)

        with stager.scope("gen-graph-gen-opts-{}".format(_shorten_debug_id(debug_id))):
            ymake_opts = dict(
                self._gen_opts(
                    flags,
                    tc,
                    test_dart_path,
                    java_dart_path,
                    make_files_dart_path,
                    current_ev_listener,
                    abs_targets,
                    debug_id=debug_id,
                    enabled_events=enabled_events,
                    extra_conf=extra_conf,
                    cache_dir=cache_dir,
                    change_list=change_list,
                    no_caches_on_retry=no_caches_on_retry,
                    no_ymake_retry=no_ymake_retry,
                ),
                **(ymake_opts or {}),
            )

        # return res, tc_tests, java_darts, make_files_map
        with stager.scope("gen-graph-json-{}".format(_shorten_debug_id(debug_id))):
            graph = self._gen_graph_json(ymake_opts, purpose=debug_id)

        if not bg.is_system(tc) and not bg.is_local(tc):
            tc_tool = _resolve_tool(tc, self._res_dir)
            graph.add_resource(tc_tool)

        if not graph.size():
            return _GenGraphResult(graph=graph, tc_tests=tc_tests, java_darts=[], make_files_map=[])

        if should_run_tc_tests:
            with stager.scope("gen-tests-{}".format(_shorten_debug_id(debug_id))):
                with open(test_dart_path) as dart:
                    tc_tests = self._gen_tests(dart.read(), tc)

        java_darts = []

        import devtools.ya.test.dartfile as td

        if java_dart_path is not None:
            with open(java_dart_path) as dart:
                for dart in td.parse_dart(dart.read().split('\n')):
                    java_dart_info = json.loads(base64.b64decode(dart['JAVA_DART']))
                    java_dart_info['_GLOBAL_RESOURCES_'] = []
                    for k, v in dart.items():
                        if k.endswith('_RESOURCE_GLOBAL'):
                            java_dart_info['_GLOBAL_RESOURCES_'].append([k, v])

                    peerdirs = java_dart_info.get('PEERDIR', '')
                    # FIXME(sidorovaa): remove isinstance check after plugins update (DEVTOOLS-5704)
                    if isinstance(peerdirs, str):
                        java_dart_info['PEERDIR'] = list(filter(bool, peerdirs.split(' ')))
                    java_darts.append(java_dart_info)

        with open(make_files_dart_path) as dart:
            make_files_map = bml.parse_make_files_dart(dart)

        if should_run_tc_tests:
            platform = tc.get('platform', {})
            target_os = platform.get('target', {}).get('os')
            target_arch = platform.get('target', {}).get('arch')

            if target_os == 'IOS':
                for test in tc_tests:
                    if (test.binary_path('') or '').endswith('.ios.tar'):
                        if not test.get_ios_device_type():
                            continue
                        if target_arch == 'x86_64':
                            test.special_runner = 'ios.simctl.x86_64'
                        elif target_arch == 'i386':
                            test.special_runner = 'ios.simctl.i386'
                    elif target_arch == 'x86_64':
                        test.special_runner = 'ios'
            elif target_os == 'IOSSIM':
                for test in tc_tests:
                    if (test.binary_path('') or '').endswith('.ios.tar'):
                        if not test.get_ios_device_type():
                            continue
                        if target_arch == 'arm64':
                            test.special_runner = 'ios.simctl.arm64'
            elif target_os == 'ANDROID':
                for test in tc_tests:
                    if not test.get_android_apk_activity():
                        continue
                    if target_arch == 'x86_64':
                        test.special_runner = 'android.x86_64'
                    elif target_arch == 'i686':
                        test.special_runner = 'android.i686'
                    elif target_arch == 'armv8a':
                        test.special_runner = 'android.armv8a'

        return _GenGraphResult(graph=graph, tc_tests=tc_tests, java_darts=java_darts, make_files_map=make_files_map)

    def _gen_opts(
        self,
        extra_flags,
        tc,
        test_dart,
        java_dart,
        make_files_dart,
        ev_listener,
        abs_targets,
        debug_id=None,
        enabled_events=YmakeEvents.DEFAULT.value,
        cache_dir=None,
        change_list=None,
        extra_conf=None,
        no_caches_on_retry=False,
        no_ymake_retry=False,
    ):
        flags = {}

        flags.update(extra_flags)
        flags.update(tc.get('params', {}).get('extra_flags', {}))
        flags.update(tc.get('flags', {}))
        flags[tc['name'].upper()] = 'yes'
        flags['YA'] = 'yes'

        if not node_checks.is_tools_tc(tc) and (_should_run_tests(self._opts, tc) or self._opts.force_build_depends):
            flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'yes'
            flags['TRAVERSE_DEPENDS'] = 'yes'
        if self._opts.ignore_recurses or tc.get('ignore_recurses'):
            flags['TRAVERSE_RECURSE'] = 'no'
            flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'no'

        build_type = gen_plan.real_build_type(tc, self._opts)
        conf_path, _ = bg.gen_conf(
            self._src_dir,
            self._conf_dir,
            _extend_build_type(build_type),
            self._opts.use_local_conf,
            self._opts.local_conf_path,
            flags,
            tc,
            not self._opts.use_distbuild,
            conf_debug=self._opts.conf_debug_options,
            debug_id=debug_id,
            extra_conf=extra_conf,
        )

        uniq_id = hashing.md5_value(str([2, self._ymake_bin]))

        ya_cache_dir = cache_dir or os.path.dirname(conf_path) + '_' + uniq_id

        if not self._heater and cache_dir and not os.path.isfile(os.path.join(ya_cache_dir, 'ymake.cache')):
            logger.debug("Cannot find ymake.cache in %s, ignore if there is no target built", ya_cache_dir)

        o = dict(
            custom_build_directory=ya_cache_dir,
            build_type=build_type,
            abs_targets=abs_targets,
            debug_options=self._opts.debug_options,
            custom_conf=conf_path,
            continue_on_fail=self._opts.continue_on_fail,
            ymake_bin=self._opts.ymake_bin,
            warn_mode=self._opts.warn_mode,
            build_depends=(
                not node_checks.is_tools_tc(tc)
                and (_should_run_tests(self._opts, tc) or self._opts.force_build_depends)
            ),
            checkout_data_by_ya=getattr(self._opts, "checkout_data_by_ya", False),
            strict_inputs=self._need_strict_inputs(flags),
            dump_inputs_map=self._is_inputs_map_required(),
            ev_listener=self._get_event_listener_debug_id_wrapper(ev_listener, debug_id, tc),
            enabled_events=enabled_events,
            no_caches_on_retry=no_caches_on_retry,
            no_ymake_retry=no_ymake_retry,
            disable_customization=strtobool(flags.get('DISABLE_YMAKE_CONF_CUSTOMIZATION', 'no')),
        )

        if test_dart:
            o["dump_tests"] = test_dart

        if java_dart:
            o['dump_java'] = java_dart

        if make_files_dart:
            o['dump_make_files'] = make_files_dart

        if change_list is None:
            if getattr(self._opts, "build_graph_cache_force_local_cl", False) and self._allow_changelist:
                if self._cl_generator is not None:
                    o['changelist_generator'] = (
                        self._cl_generator
                    )  # TODO: think of a proper way to pass it closer to ymake launch
                    o['patch_path'] = self._cl_generator.get_changelist(ya_cache_dir)
                    if o['patch_path'] is not None and 'completely-trust-fs-cache' not in o['debug_options']:
                        o['debug_options'] = o['debug_options'] + ['completely-trust-fs-cache']
        else:
            o['patch_path'] = change_list

        if self._opts.dump_file_path:
            assert debug_id
            exts.fs.ensure_dir(self._opts.dump_file_path)
            # Truncate filename to be sure it can be created on the FS
            if len(debug_id) > 150:
                suffix = "-{md5}".format(md5=hashing.md5_value(debug_id))
                filename = debug_id[: 150 - len(suffix)] + suffix + '.txt'
            else:
                filename = debug_id + '.txt'
            o['dump_file'] = os.path.join(self._opts.dump_file_path, filename)

        if self._opts.compress_ymake_output:
            assert self._opts.compress_ymake_output_codec, "Compress codec must be defined"
            o['compress_ymake_output_codec'] = self._opts.compress_ymake_output_codec

        if self._opts.ymake_multiconfig:
            o['multiconfig'] = self._opts.ymake_multiconfig

        return o

    def _need_strict_inputs(self, flags):
        return self._opts.strict_inputs or flags.get('SANDBOXING') == 'yes'

    def _is_inputs_map_required(self):
        return (
            self._opts.use_distbuild and self._opts.repository_type == distbs_consts.DistbuildRepoType.TARED
        ) or self._opts.run_tagged_tests_on_sandbox

    def _event_listener_invalid_recurses_wrapper(self, ev_listener, tc):
        if self._opts.dump_platform_to_evlog:

            def enrich_invalid_recurses_listener(event):
                if event['_typename'] in ('NEvent.TInvalidRecurse', 'NEvent.TDisplayMessage', 'NEvent.TFailOnRecurse'):
                    event['Platform'] = pm.stringize_toolchain(tc)
                ev_listener(event)

            return enrich_invalid_recurses_listener
        else:
            return ev_listener

    def _get_event_listener_debug_id_wrapper(self, ev_listener, debug_id, tc):
        event_listener_invalid_recurses_wrapper_func = self._event_listener_invalid_recurses_wrapper(ev_listener, tc)

        def event_listener_debug_id_wrapper(event):
            if (
                event['_typename'] in ("NEvent.TStageStarted", "NEvent.TStageFinished")
                and event["StageName"] == "ymake run"
            ):
                event["debug_id"] = debug_id
            event_listener_invalid_recurses_wrapper_func(event)

        return event_listener_debug_id_wrapper

    def _gen_tests(self, dart, tc):
        import devtools.ya.test.explore as te

        timer = exts.timer.Timer('gen_tests')

        target_platform_descriptor = _get_target_platform_descriptor(tc, self._opts)
        if self._opts.test_diff:
            tests = te.generate_diff_tests(self._opts, target_platform_descriptor)
        else:
            multi_target_platform_run = len(self._test_target_platforms) > 1
            with_wine = None
            if self._needs_wine64_for_tests([tc]):
                with_wine = 'wine64'
            elif self._needs_wine32_for_tests([tc]):
                with_wine = 'wine32'
            tests = list(
                te.generate_tests_by_dart(
                    dart,
                    target_platform_descriptor,
                    multi_target_platform_run,
                    self._opts,
                    with_wine=with_wine,
                )
            )
        logger.debug("Found %d tests", len(tests))
        timer.show_step('generate tests')
        return tests

    @staticmethod
    def _make_debug_id(text, ispic):
        assert text
        assert '-{ispic}' in text
        if ispic is None:
            return text.replace('-{ispic}', '')
        return text.format(ispic=ispic)

    def _gen_graph_json(self, ymake_opts, purpose) -> ccgraph.Graph:
        if self._heater:
            purpose = (purpose or '') + 'heater'
            diag_key = 'build-graph-cache-heater'
        else:
            diag_key = 'ymake-dump-graph'
        ymake_res, _ = ymake2.ymake_gen_graph(
            grab_stderr=True,
            mode='dist',
            dump_graph='json',
            check=self._check,
            _purpose=purpose,
            cpp=True,
            **ymake_opts,
        )
        if app_config.in_house:
            import yalibrary.diagnostics as diag

            if diag.is_active():
                logger.debug('Diag mode: additionally call dump graph.')
                dump_opts = ymake_opts.copy()
                dump_opts['debug_options'] = ['g'] + dump_opts.get('debug_options', [])
                dump, _ = ymake2.ymake_dump(grab_stderr=True, **dump_opts)
                diag.save(diag_key, graph=dump.stdout)

        try:
            with stager.scope('load-graph-from-json'):
                return ccgraph.Graph(ymake_output=ymake_res.stdout)
        except Exception as e:
            raise GraphMalformedException(str(e))

    @staticmethod
    def _needs_wine64_for_tests(test_target_platforms):
        return _needs_wine_for_tests(test_target_platforms, ('x86_64',))

    @staticmethod
    def _needs_wine32_for_tests(test_target_platforms):
        return _needs_wine_for_tests(
            test_target_platforms,
            (
                'i386',
                'i686',
            ),
        )


def _build_graph_and_tests(
    opts, check, event_queue, exit_stack, display
):  # ?, True, devtools.ya.core.event_handling.event_queue.EventQueue, contextlib2.ExitStack, yalibrary.display.Display
    import devtools.ya.core.config

    build_graph_and_tests_stage = stager.start('build_graph_and_tests')

    cl_generator = _prepare_local_change_list_generator(opts)

    print_status = get_print_status_func(opts, display, logger)

    conf_error_reporter = _ConfErrorReporter(event_queue)

    if 'CC' in os.environ or 'CXX' in os.environ:
        logger.info(
            "Attention! CC and CXX env.vars are ignored: use --c(xx)-compiler/--target-platform-c(xx)-compiler options to specify user-defined compiler."
        )

    bld_dir = opts.bld_dir
    res_dir = devtools.ya.core.config.tool_root(toolscache_version(opts))
    src_dir = opts.arc_root

    vcs_info = exit_stack.enter_context(
        _AsyncContext(
            core_async.future(
                lambda: get_version_info(
                    src_dir,
                    bld_dir,
                    opts.vcs_file,
                    opts.flags,
                    custom_version=getattr(opts, 'custom_version', ''),
                    release_version=getattr(opts, 'release_version', ''),
                ),
                daemon=False,
            )
        )
    )

    host = opts.host_platform
    target_platforms = opts.target_platforms

    if host:
        host = bg.mine_platform_name(host)

    if target_platforms:
        target_platforms = [x.copy() for x in target_platforms]
        for platform in target_platforms:
            platform_name = platform['platform_name']
            if platform_name == "host_platform":
                platform_name = bg.host_platform_name()
            platform['platform_name'] = bg.mine_platform_name(platform_name)

    if opts.use_distbuild:
        if not host:
            host = bg.mine_platform_name('default-linux-x86_64')
        if not target_platforms:
            target_platforms = [{'platform_name': bg.host_platform_name()}]
    else:
        if pm.is_darwin_rosetta() and not opts.hide_arm64_host_warning and not host and not target_platforms:
            try:
                import app_ctx

                app_ctx.display.emit_message(
                    "Current target architecture is x86_64. Specify \"--target-platform\" flag to build for native arm64."
                )
            except Exception as e:
                logger.error("Can't print arm64 warning message: {}".format(e))
        if not host:
            host = bg.host_platform_name()
        if not target_platforms:
            target_platforms = [{'platform_name': host}]

    parsed_host_p = pm.parse_platform(host)
    host_os = parsed_host_p['os']
    # https://st.yandex-team.ru/DTCC-277
    # host_platform = 'darwin-arm64' if host_os.lower() == 'darwin' and parsed_host_p.get('arch') == 'arm64' else host_os
    host_platform = host_os

    target_flags = dict(_iter_target_flags(opts))

    if target_flags:
        for t in target_platforms:
            t['flags'] = dict(target_flags, **t.get('flags', {}))
    # done propagate

    # add salt for target platforms in sandboxing mode
    sandboxing_salt = 'sandboxing'
    for t in target_platforms:
        if t.get('flags', {}).get('SANDBOXING') == 'yes':
            t['flags']['FAKEID'] = t['flags'].get('FAKEID', '') + sandboxing_salt

    if len(target_platforms) > 1 and opts.canonize_tests:
        raise devtools.ya.core.yarg.ArgsBindingException(
            'Canonization is not supported for more than one target platform'
        )

    host_tc = bg.gen_tc(
        host, opts.c_compiler, opts.cxx_compiler, opts.flags.get('IGNORE_MISMATCHED_XCODE_VERSION') == 'yes'
    )
    host_tc['build_type'] = opts.host_build_type
    host_tc['flags'] = {
        'NO_DEBUGINFO': 'yes',
        'TOOL_BUILD_MODE': 'yes',
        'CLANG_COVERAGE': 'no',
        'TRAVERSE_RECURSE': 'no',
        'CONSISTENT_DEBUG': 'yes',
        "TIDY": 'no',
    }
    if opts.host_platform_flags:
        host_tc['flags'].update(opts.host_platform_flags)
    if opts.preset_mapsmobi:
        host_tc['flags']['MAPSMOBI_BUILD_HOST'] = 'yes'
    if opts.sandboxing:
        host_tc['flags']['SANDBOXING'] = 'yes'
    if host_tc['flags'].get('SANDBOXING') == 'yes':
        host_tc['flags']['FAKEID'] = host_tc['flags'].get('FAKEID', '') + sandboxing_salt

    def resolve_target(target):
        target = copy.deepcopy(target)
        target.update(
            bg.gen_cross_tc(
                host,
                target['platform_name'],
                target.get('c_compiler', opts.c_compiler),
                target.get('cxx_compiler', opts.cxx_compiler),
                (
                    target.get('flags', {}).get('IGNORE_MISMATCHED_XCODE_VERSION') == 'yes'
                    or opts.flags.get('IGNORE_MISMATCHED_XCODE_VERSION') == 'yes'
                ),
            )
        )
        return target

    target_tcs = [resolve_target(p) for p in target_platforms]
    test_target_platforms = [tpc for tpc in target_tcs if _should_run_tests(opts, tpc)]

    logger.debug('flags: %s', json.dumps(opts.flags, indent=4, sort_keys=True))
    logger.debug('host toolchain: %s', json.dumps(host_tc, indent=4, sort_keys=True))

    for tc in target_tcs:
        logger.debug('target toolchain: %s', json.dumps(tc, indent=4, sort_keys=True))

    platforms_info = {
        'host': {
            'name': host_tc['name'],
            'platform': host_tc['platform'],
        },
        'targets': [
            {
                'name': target_tc['name'],
                'platform': target_tc['platform'],
            }
            for target_tc in target_tcs
        ],
    }
    devtools.ya.core.report.telemetry.report(devtools.ya.core.report.ReportTypes.PLATFORMS, platforms_info)
    yalibrary.debug_store.dump_debug['platforms'] = platforms_info

    timer = exts.timer.Timer(__name__)
    ymake_bin = opts.ymake_bin

    if ymake_bin:
        real_ymake_bin = ymake_bin
    else:
        # ensure gen_graph() will not fetch anything
        ymake_bin = '$(YMAKE)/ymake'
        real_ymake_bin = tools.tool('ymake')
        timer.show_step('fetch ymake')

    _enable_imprint_fs_cache(opts)

    graph_maker = _GraphMaker(
        opts,
        ymake_bin,
        real_ymake_bin,
        src_dir,
        bld_dir,
        res_dir,
        event_queue,
        test_target_platforms,
        check,
        exit_stack,
        print_status,
        cl_generator,
    )

    host_tool_resolver = _HostToolResolver(parsed_host_p, res_dir)

    graph_handles: list[_TargetGraphsResult] = []
    tool_targets_queue = create_tool_event_queue(opts)
    enabled_events = EVENTS_WITH_PROGRESS + YmakeEvents.PREFETCH.value if opts.prefetch else EVENTS_WITH_PROGRESS
    for i, tc in enumerate(target_tcs, start=1):
        targets = []
        for target in tc.get('targets', []):
            targets.append(os.path.join(src_dir, target))
        if len(target_tcs) > 1:
            debug_id = 'target_tc%d-{ispic}-%s' % (i, _get_target_platform_descriptor(tc, opts))
        else:
            debug_id = _get_target_platform_descriptor(tc, opts) + '-{ispic}'
        target_graph = graph_maker.make_graphs(
            tc,
            targets or opts.abs_targets,
            debug_id=debug_id,
            extra_conf=opts.extra_conf,
            enabled_events=enabled_events,
            tool_targets_queue=tool_targets_queue,
        )
        graph_handles.append(target_graph)

    with stager.scope("get-tools"):
        graph_tools = _get_tools(tool_targets_queue, graph_maker, opts.arc_root, host_tc, opts)

    graph_maker.disable_changelist()

    any_tests = any([_should_run_tests(opts, tc) for tc in target_tcs])

    # Note: run merge_target_graphs() in parallel (in different threads) is unsafe because graph_tools is shared between target graphs.
    merged_target_graphs: list[_MergeTargetGraphResult] = []
    for num, target_graph in enumerate(graph_handles, start=1):
        graph = _merge_target_graphs(
            graph_maker,
            conf_error_reporter,
            opts,
            any_tests,
            graph_tools,
            target_graph,
            num,
        )
        merged_target_graphs.append(graph)

    with stager.scope('build-merged-graph'):
        graph, tests, stripped_tests, make_files = _build_merged_graph(
            host_tool_resolver,
            conf_error_reporter,
            opts,
            print_status,
            src_dir,
            any_tests,
            target_tcs,
            merged_target_graphs,
        )

    ctx = None
    if opts.flags.get('YA_IDE_IDEA') == 'yes':
        with stager.scope('ya-ide-idea'):
            graph, ctx = _prepare_for_ya_ide_idea(graph_maker, opts, event_queue, graph_handles[0], graph)

    with stager.scope('iter-extra-resources'):
        for pattern, name in _iter_extra_resources(graph):
            graph['conf']['resources'] = graph['conf'].get('resources', []) + [
                host_tool_resolver.resolve(name, pattern)
            ]

    timer.show_step('build and merge graphs')

    if opts.strip_packages_from_results:
        with stager.scope('strip_packages_from_results'):
            graph = _strip_graph_results(graph, node_checks.is_package, "package")

    if opts.strip_binary_from_results:
        with stager.scope('strip_binaries_from_results'):
            graph = _strip_graph_results(graph, node_checks.is_binary, "binary")

    with stager.scope('strip-graph'):
        graph = strip_graph(graph)
        timer.show_step('strip graph')

    if opts.gen_renamed_results:
        # TODO: this works incorrectly with tests outputs
        by_uid = {n['uid']: n for n in graph['graph']}
        graph = _gen_rename_nodes(graph, by_uid, src_dir)
        timer.show_step('gen rename nodes')

    graph['result'] = list(sorted(set(graph['result'])))

    graph['conf']['resources'].append(host_tool_resolver.resolve('python', 'PYTHON'))

    if not opts.ymake_bin:
        graph['conf']['resources'].append(host_tool_resolver.resolve('ymake', 'YMAKE'))

    graph['conf']['resources'].append(vcs_info())

    graph['conf'] |= {
        'keepon': opts.continue_on_fail,
        'cache': not opts.clear_build,
        'platform': host_platform.lower(),
        'graph_size': len(graph['graph']),
        'execution_cost': {'cpu': 0, 'evaluation_errors': 0},
        'min_reqs_errors': 0,
        'explicit_remote_store_upload': True,
    }

    if 0:
        graph['graph'] = list(_split_gcc(graph['graph']))

    if 0:
        graph = _optimize_graph(graph)

    if opts.yndexing:
        graph = _make_yndexing_graph(graph, opts, ymake_bin, host_tool_resolver)

    if opts.export_to_maven:
        graph = _add_global_pom(
            _clean_maven_deploy_from_run_java_program(graph),
            opts.arc_root,
            opts.rel_targets,
            opts.version,
        )

    if opts.add_modules_to_results:
        graph = _add_modules_to_results(graph)

    json_prefix = opts.json_prefix

    if json_prefix:
        with stager.scope('change_uid_prefix'):
            _add_json_prefix(graph, tests, json_prefix)

    if opts.share_results:
        graph = _set_share_for_results(graph)

    if opts.default_node_requirements:
        with stager.scope('inject_default_requirements'):
            _inject_default_requirements(graph, tests, opts.default_node_requirements)

    _propagate_cache_false_from_kv(graph)

    timer.show_step('misc graph changes')
    # Temporary
    # Need to overclock
    if opts.pgo_path:
        pgo_hash = _add_pgo_profile_resource(graph, opts.pgo_path)
        _transform_nodes_uids(graph, _transformer_for_pgo(pgo_hash))

    with stager.scope("finalize-graph"):
        finalize_graph(graph, opts)
    with stager.scope("imprint-store-and-stats"):
        imprint.store()
        imprint.stats()
    bg_cache.archive_cache_dir(opts)

    with stager.scope('inject_stats_and_static_uids'):
        gen_plan.inject_stats_and_static_uids(graph)

    with stager.scope("strip-tags"):
        graph['graph'] = _strip_tags(graph['graph'])
        timer.show_step('strip tags')

    if opts.graph_stat_path:
        stat = _load_stat(opts.graph_stat_path)

        nodes_stat_path_filters = opts.nodes_stat_path_filters.split(';') if opts.nodes_stat_path_filters else []
        _add_stat_to_graph(graph, stat, nodes_stat_path_filters)

    if opts.dump_graph_execution_cost:
        with open(opts.dump_graph_execution_cost, 'w') as f:
            f.write(json.dumps(graph['conf'].get('execution_cost', {})))

    graph = _OptimizableGraph(graph)
    graph.optimize_resources()

    with stager.scope('clean-intern-string-storage'):
        # After this point all ccgraphs become useless (all python graphs remain good).
        # Expected that all ccgraph are destroyed long before.
        ccgraph.clean_intern_string_storage()

    build_graph_and_tests_stage.finish()

    return graph, tests, stripped_tests, ctx, make_files


def _should_run_tests(opts, target_tc):
    return not node_checks.is_tools_tc(target_tc) and (
        opts.run_tests or target_tc.get('run_tests') or opts.list_tests or opts.canonize_tests
    )


def _enable_imprint_fs_cache(opts):
    if opts.cache_fs_read or opts.cache_fs_write:
        imprint_enable_fs_cache_stage = stager.start('imprint_enable_fs_cache')
        try:
            imprint.enable_fs(
                read=opts.cache_fs_read,
                write=opts.cache_fs_write,
                cache_source_path=bg_cache.configure_build_graph_cache_dir(opts),
                process_arcadia_clash=False,
                quiet=True,
            )

            imprint.use_change_list(opts.build_graph_cache_cl, quiet=True)
            logger.debug("imprint fs cache is enabled")
            YaMonEvent.send('EYaStats::ImprintFSCacheEnabled', True)
        except Exception:
            logger.exception("Something goes wrong while enabling fs cache / changelist")
            imprint.disable_fs()
        finally:
            imprint_enable_fs_cache_stage.finish()


# propagate some flags to all target platforms...
def _iter_target_flags(opts):
    for opt_val, flag in [
        (opts.sanitize, 'SANITIZER_TYPE'),
        (opts.sanitize_coverage, 'SANITIZE_COVERAGE'),
    ]:
        if not opt_val:
            opt_val = opts.flags.get(flag, None)
            if opt_val and flag in opts.flags:
                del opts.flags[flag]

        if opt_val:
            yield flag, opt_val

    if opts.yndexing:
        yield 'CODENAVIGATION', 'yes'

    if opts.sanitizer_flags:
        yield 'SANITIZER_CFLAGS', ' '.join(opts.sanitizer_flags)

    if opts.lto:
        yield 'USE_LTO', 'yes'

    if opts.thinlto:
        yield 'USE_THINLTO', 'yes'

    if opts.musl:
        yield 'MUSL', 'yes'

    if opts.use_afl:
        yield 'USE_AFL', 'yes'

    if opts.pgo_add:
        yield 'PGO_ADD', 'yes'

    if opts.pgo_path:
        yield 'PGO_PATH', '$(PGO_PATH)'

    if opts.javac_flags:
        yield 'JAVAC_OPTS', ' '.join(['-{} {}'.format(k, v or '') for k, v in opts.javac_flags.items()])

    if opts.hardening:
        yield 'HARDENING', 'yes'

    if opts.sandboxing:
        yield 'SANDBOXING', 'yes'

    if opts.race:
        yield 'RACE', 'yes'

    if opts.cuda_platform == 'disabled':
        yield 'CUDA_DISABLED', 'yes'
        yield 'HAVE_CUDA', 'no'
    if opts.cuda_platform == 'required':
        yield 'CUDA_REQUIRED', 'yes'

    if opts.preset_mapsmobi:
        yield 'MAPSMOBI_BUILD_TARGET', 'yes'

    if opts.preset_with_credits:
        yield 'WITH_CREDITS', 'yes'


def _needs_wine_for_tests(test_target_platforms, archs=None):
    for test_target_platform in test_target_platforms:
        if (
            test_target_platform['platform']['host']['os'] == "LINUX"
            and test_target_platform['platform']['target']['os'] == "WIN"
        ):
            if not archs:
                return True
            if test_target_platform['platform']['target']['arch'] in archs:
                return True
    return False


# See build_graph_and_tests::iter_target_flags
def _get_target_platform_descriptor(target_tc, opts):
    tags, platform = gen_plan.prepare_tags(target_tc, {}, opts)
    return "-".join(tags) if tags else platform


def _add_global_pom(graph: graph_descr.DictGraph, arc_root, start_paths, ver):
    modules = [
        n['target_properties']['module_dir'] for n in graph['graph'] if n.get('kv', {}).get('mvn_export', 'no') == 'yes'
    ]
    if len(modules) == 0:
        return graph

    inputs = [
        "build/scripts/fs_tools.py",
        "build/scripts/mkdir.py",
        "build/scripts/writer.py",
        "build/scripts/process_command_files.py",
    ]
    cmds = [
        [
            "$(PYTHON)/python",
            '$(SOURCE_ROOT)/build/scripts/writer.py',
            '--file',
            '$(BUILD_ROOT)/modules_list.txt',
            '-m',
            '--ya-start-command-file',
        ]
        + [d for d in modules]
        + ['--ya-end-command-file'],
        [
            '$(PYTHON)/python',
            '$(SOURCE_ROOT)/build/scripts/generate_pom.py',
            '--target',
            'ru.yandex:root-for-{}:{}'.format(
                '-'.join(root.strip('/').replace('/', '-') for root in start_paths),
                ver if ver is not None else '{vcs_revision}',
            ),
            '--pom-path',
            '$(BUILD_ROOT)/pom.xml',
            '--property',
            'arcadia.root=$(SOURCE_ROOT)',
            '--packaging',
            'pom',
            '--modules-path',
            '$(BUILD_ROOT)/modules_list.txt',
            '--vcs-info',
            '$(VCS)/vcs.json',
        ],
    ]

    node = graph_node.Node(
        path='',
        cmds=[graph_cmd.Cmd(cmd, None, []) for cmd in cmds],
        ins=[(os.path.join(graph_const.SOURCE_ROOT, f), graph_const.FILE) for f in inputs],
        outs=[(os.path.join(graph_const.BUILD_ROOT, 'pom.xml'), graph_const.FILE)],
        kv={'p': 'JV', 'pc': 'light-blue'},
    )
    node.calc_node_uid(arc_root)
    graph['graph'].append(node.to_serializable())
    graph['result'].append(node.uid)
    return graph


# TODO(YMAKE-216) remove this function when RUN_JAVA_PROGRAM node with its deps is taken from TOOL platform
def _clean_maven_deploy_from_run_java_program(graph):
    all_exported = {n['uid']: n for n in graph['graph'] if n.get('kv', {}).get('mvn_export', None) == 'yes'}

    reachable_exported = set()
    queue = collections.deque(res for res in graph['result'] if res in all_exported)
    while queue:
        uid = queue.popleft()
        queue.extend(dep for dep in all_exported[uid].get('deps', []) if dep in all_exported)
        reachable_exported.add(uid)

    for uid, node in all_exported.items():
        if uid in reachable_exported:
            continue
        node['kv']['mvn_export'] = 'no'
        node['cmds'] = [cmd for cmd in node['cmds'] if not node_checks.is_maven_deploy(cmd)]
        logging.debug(
            'Disable maven export of TOOL only java module "{}" uid: {}'.format(
                node.get('target_properties', {}).get('module_dir', 'NOT A MODULE'), uid
            )
        )
    return graph


def _make_yndexing_graph(graph: graph_descr.DictGraph, opts, ymake_bin, host_tool_resolver: "_HostToolResolver"):
    py_yndexing = opts.py_yndexing
    py3_yndexing = opts.py3_yndexing

    graph['conf']['resources'].append(host_tool_resolver.resolve('ytyndexer', 'YTYNDEXER'))
    graph['conf']['resources'].append(host_tool_resolver.resolve('python', 'PYTHON'))

    if opts.flags.get('YMAKE_YNDEXING', 'no') == 'yes':
        _gen_ymake_yndex_node(graph, opts, ymake_bin, host_tool_resolver)
    if py_yndexing:
        _gen_pyndex_nodes(graph)  # TODO: Remove?
    if py3_yndexing:
        _gen_py3_yndexer_nodes(graph)

    yndexing_nodes = sorted([n for n in graph['graph'] if _find_yndex_file(n)], key=lambda z: z['outputs'])
    merging_nodes, alone_nodes = _gen_merge_nodes(yndexing_nodes)
    graph['result'] = [n['uid'] for n in merging_nodes + alone_nodes]
    graph['graph'].extend(merging_nodes)

    if opts.yt_cluster and opts.yt_root:
        uploading_nodes = [_gen_upload_node(opts, n) for n in merging_nodes + alone_nodes]
        graph['result'].extend([n['uid'] for n in uploading_nodes])
        graph['graph'].extend(uploading_nodes)

    for node in yndexing_nodes:
        node['timeout'] = 30 * 60

    graph = strip_graph(graph)

    return graph


def _gen_upload_node(opts, nd):
    yndex_file = _find_yndex_file(nd)
    output_file = yndex_file + '.yt'
    cmd = [
        '$(YTYNDEXER)/ytyndexer',
        'upload',
        '-y',
        '$(BUILD_ROOT)',
        '-r',
        opts.yt_root,
        '-c',
        opts.yt_cluster,
        '--dump-statistics',
        output_file,
    ]
    node_tag = 'yt_upload'
    opts_that_do_not_affect_uid = []
    if opts.yt_codenav_extra_opts is not None:
        yt_codenav_extra_opts = list(filter(None, opts.yt_codenav_extra_opts.split(' ')))
        if yt_codenav_extra_opts != ['no']:
            # Note: it is assumed that the upload options do not affect the uid
            opts_that_do_not_affect_uid.extend(yt_codenav_extra_opts)
        if '--use-yt-dynamic-tables' in yt_codenav_extra_opts:
            node_tag = 'yt_upload_dynamic'
    uid = 'yy-upload-{}'.format(hashing.md5_value(nd['uid'] + '##' + ' '.join(cmd)))
    cmd.extend(opts_that_do_not_affect_uid)
    if not opts.yt_readonly:
        cmd += ['-u', uid]
    node = {
        'cmds': [{'cmd_args': cmd}],
        'kv': {'p': 'YU', 'pc': 'magenta'},
        'outputs': [output_file],
        'inputs': [yndex_file],
        'deps': [nd['uid']],
        'tag': node_tag,
        'cache': nd.get('cache', True),
        'backup': True,
        'requirements': {'network': 'full'},
        'secrets': ['YT_YNDEXER_YT_TOKEN'],
        'timeout': 60 * 60,
        'uid': uid,
        'type': 2,
    }

    if opts.default_node_requirements:
        _add_requirements(node, opts.default_node_requirements)

    return node


def _gen_merge_node(nodes):
    deps = [n['uid'] for n in nodes]
    output_file = '$(BUILD_ROOT)/{}.ydx.pb2'.format(hashing.md5_value(' '.join(deps)))
    cmd = ['$(YTYNDEXER)/ytyndexer', 'merge', '-y', '$(BUILD_ROOT)', '-t', output_file]
    uid = 'yy-merge-{}'.format(hashing.md5_value(' '.join(deps) + '#' + ' '.join(cmd)))
    return {
        'cmds': [{'cmd_args': cmd}],
        'kv': {'p': 'YM', 'pc': 'magenta'},
        'outputs': [output_file],
        'timeout': 40 * 60,
        'inputs': [],
        'deps': deps,
        'uid': uid,
        'type': 2,
    }


def _gen_ymake_yndex_node(graph, opts, ymake_bin, host_tool_resolver: "_HostToolResolver"):
    tc = bg.gen_host_tc(getattr(opts, 'c_compiler', None), getattr(opts, 'cxx_compiler', None))
    tc_params = six.ensure_str(base64.b64encode(six.ensure_binary(json.dumps(tc, sort_keys=True))))
    conf_flags = []
    for f in ('RECURSE_PARTITIONS_COUNT', 'RECURSE_PARTITION_INDEX'):
        val = opts.flags.get(f)
        if val is not None:
            conf_flags += ['-D', '{}={}'.format(f, val)]

    ymakeyndexer_cmd = None
    if ymakeyndexer_override := opts.flags.get('TOOL_YMAKEYNDEXER'):
        ymakeyndexer_cmd = ymakeyndexer_override
    else:
        resource_json = host_tool_resolver.resolve('ymakeyndexer', 'YMAKEYNDEXER')
        graph['conf']['resources'].append(resource_json)
        ymakeyndexer_cmd = '$({})/ymakeyndexer'.format(resource_json['pattern'])

    cmds = [
        {
            'cmd_args': [
                "$(PYTHON)/python",
                '$(SOURCE_ROOT)/build/ymake_conf.py',
                '$(SOURCE_ROOT)',
                'nobuild',
                'no',
                '--toolchain-params',
                tc_params,
            ]
            + conf_flags,
            'stdout': '$(BUILD_ROOT)/ymake.conf',
        },
        {
            'cmd_args': [
                ymake_bin,
                '-B',
                '$(BUILD_ROOT)',
                '-y',
                '$(SOURCE_ROOT)/build/plugins',
                '-y',
                '$(SOURCE_ROOT)/build/internal/plugins',
                '-c',
                '$(BUILD_ROOT)/ymake.conf',
                '-Y',
                '$(BUILD_ROOT)/ymake.ydx.json',
            ]
            + [os.path.join('$(SOURCE_ROOT)', rel_target) for rel_target in opts.rel_targets]
        },
        {
            'cmd_args': [
                ymakeyndexer_cmd,
                '-i',
                '$(BUILD_ROOT)/ymake.ydx.json',
                '-o',
                '$(BUILD_ROOT)/ymake.ydx.pb2',
            ]
        },
    ]

    uid: graph_descr.GraphNodeUid = 'yy-yndex-{}'.format(exts.uniq_id.gen16())
    yndexing_node = {
        'cmds': cmds,
        'kv': {'p': 'YY', 'pc': 'magenta'},
        'outputs': ['$(BUILD_ROOT)/ymake.ydx.pb2'],
        'timeout': 60 * 60,
        'inputs': ['$(SOURCE_ROOT)/build/ymake_conf.py'],
        'deps': [],
        'uid': uid,
        'type': 2,
        'cache': False,
    }

    graph['graph'].extend([yndexing_node])


def _gen_pyndex_nodes(graph: graph_descr.DictGraph, num_partitions: int = 10):
    yndexer_script = '$(SOURCE_ROOT)/build/scripts/python_yndexer.py'
    pyxref = next(
        os.path.join('$(' + r['pattern'] + ')', 'pyxref')
        for r in graph['conf']['resources']
        if r['pattern'].startswith('PYNDEXER-')
    )

    pyndex_nodes = []
    for node in graph['graph']:
        target = node.get('kv', {}).get('pyndex')
        if not target:
            continue
        for part_id in range(num_partitions):
            ydx_output = "{}.{}.ydx.pb2".format(target, part_id)
            cmd = [
                "$(PYTHON)/python",
                yndexer_script,
                pyxref,
                '1500',
                ydx_output,
                target,
                str(num_partitions),
                str(part_id),
            ]
            deps = [node['uid']]
            uid = 'yy-pyndex-{}'.format(hashing.md5_value(' '.join(deps) + '#' + ' '.join(cmd)))
            part_node = {
                'cmds': [{'cmd_args': cmd}],
                'kv': {'p': 'YPY', 'pc': 'magenta'},
                'outputs': [ydx_output],
                'timeout': 15 * 60,
                'inputs': [yndexer_script, target],
                'deps': deps,
                'uid': uid,
                'type': 2,
            }
            pyndex_nodes.append(part_node)
    graph['graph'].extend(pyndex_nodes)


def _gen_py3_yndexer_nodes(graph):
    py3_indexer = next(
        os.path.join('$(' + r['pattern'] + ')', 'py3yndexer')
        for r in graph['conf']['resources']
        if r['pattern'].startswith('PY3YNDEXER-')
    )

    pyndex_nodes = []
    for node in graph['graph']:
        target = node.get('kv', {}).get('py3yndex')

        if not target:
            continue

        ydx_output = "{}.ydx.pb2".format(target)

        cmd = [
            py3_indexer,
            "--yndex-file",
            ydx_output,
            "--program-file",
            target,
        ]

        deps = [node['uid']]
        uid = 'yy-py3yndex-{}'.format(hashing.md5_value(' '.join(deps) + '#' + ' '.join(cmd)))
        node = {
            'cmds': [{'cmd_args': cmd}],
            'kv': {'p': 'YPY3', 'pc': 'magenta'},
            'outputs': [ydx_output],
            'timeout': 15 * 60,
            'inputs': [target],
            'deps': deps,
            'uid': uid,
            'type': 2,
        }
        pyndex_nodes.append(node)

    graph['graph'].extend(pyndex_nodes)


def _find_yndex_file(node):
    files = list([o for o in node['outputs'] if o.endswith('.ydx.pb2')])
    return files[0] if files else None


def _gen_merge_nodes(nodes):
    alone_nodes = []
    merging_nodes = []
    nodes_by_dir = collections.defaultdict(list)

    for node in nodes:
        yndex_file = _find_yndex_file(node)
        if yndex_file:
            nodes_by_dir[os.path.dirname(yndex_file)].append(node)

    for deps in nodes_by_dir.values():
        if len(deps) == 1:
            alone_nodes.append(deps[0])
        else:
            merging_nodes.append(_gen_merge_node(deps))
    return merging_nodes, alone_nodes


def _get_tools(tool_targets_queue, graph_maker: _GraphMaker, arc_root, host_tc, opts):
    if should_use_servermode_for_tools(opts):

        def stdin_line_provider():
            # This is a workaround. TShellCommand pulls stdin anytime the child process tries to write to any of std{out,err}.
            # So we must not block here if we know there wont be any new events in the queue.
            # Returning an empty string effectively closes the stream.
            # TODO: we should throw an exception here instead to make the user responsible of closing the stream.
            if tool_targets_queue.done():
                return ''
            return json.dumps(tool_targets_queue.get()) + '\n'

        kwargs = {
            'abs_targets': [],
            'ymake_opts': {
                'targets_from_evlog': True,
                'source_root': arc_root,
                'stdin_line_provider': stdin_line_provider,
            },
        }
    else:
        with stager.scope("waiting-tool-targets"):
            tools_targets = tool_targets_queue.get()
        abs_targets = [os.path.join(arc_root, tt) for tt in tools_targets]

        if not abs_targets:
            logger.debug("Empty tool targets list")
            return ccgraph.get_empty_graph()

        kwargs = {
            'abs_targets': abs_targets,
        }

    with stager.scope('build-tool-graphs'):
        tg = graph_maker.make_graphs(
            host_tc,
            graph_kind=_GraphKind.TOOLS,
            debug_id='tools-{ispic}',
            enabled_events=EVENTS_WITH_PROGRESS,
            extra_conf=opts.extra_conf,
            **kwargs,
        )
        if opts.ymake_multiconfig:
            ymake2.run_ymake_scheduled(graph_maker.ymakes_scheduled)
        graph_tools = tg.pic().graph

    graph_tools.add_host_mark(strtobool(host_tc['flags'].get('SANDBOXING', "no")))
    graph_tools.add_tool_deps()

    return graph_tools


class _HostToolResolver:
    def __init__(self, parsed_host_p, res_dir):
        self._host_tool = self.generate_host_platform_str(parsed_host_p)
        self._res_dir = res_dir

    @staticmethod
    def generate_host_platform_str(parsed_host_p):
        host_tool = parsed_host_p.copy()
        host_tool['toolchain'] = 'default'
        return pm.stringize_platform(host_tool)

    def resolve(self, name: str, pattern: str) -> graph_descr.GraphConfResourceInfo:
        tc = tools.resolve_tool(name, self._host_tool, self._host_tool)
        tc['params'] = {'match_root': pattern}
        return _resolve_tool(tc, self._res_dir)


def _resolve_test_tools(graph_maker, toolchain, debug_id, opts):
    targets = [
        os.path.join('build', 'platform', 'test_tool'),
        os.path.join('build', 'external_resources', 'go_tools'),
        os.path.join('build', 'external_resources', 'flake8_py2'),
        os.path.join('build', 'external_resources', 'flake8_py3'),
        os.path.join('build', 'platform', 'java', 'jstyle_lib'),
    ]
    resources = [
        tconst.TEST_TOOL_HOST,
        tconst.TEST_TOOL_TARGET,
        tconst.XCODE_TOOLS_RESOURCE,
        tconst.GO_TOOLS_RESOURCE,
        tconst.FLAKE8_PY2_RESOURCE,
        tconst.FLAKE8_PY3_RESOURCE,
        tconst.JSTYLE_RUNNER_LIB,
    ]

    if _needs_wine_for_tests([toolchain]):
        targets += [os.path.join('build', 'platform', 'wine')]
        resources += [
            tconst.WINE_TOOL,
            tconst.WINE32_TOOL,
        ]

    if getattr(opts, 'ts_coverage', None):
        targets.append(os.path.join('build', 'platform', 'nodejs'))
        resources.append(tconst.NODEJS_RESOURCE)
        targets.append(os.path.join('build', 'external_resources', 'nyc'))
        resources.append(tconst.NYC_RESOURCE)

    return _resolve_global_tools(graph_maker, toolchain, opts, targets, resources, debug_id)


def _resolve_test_tool_only(graph_maker, toolchain, debug_id, opts):
    targets = [os.path.join('build', 'platform', 'test_tool')]
    resources = [tconst.TEST_TOOL_HOST]
    return _resolve_global_tools(graph_maker, toolchain, opts, targets, resources, debug_id)


def _resolve_global_tools(graph_maker, toolchain, opts, targets, resources, debug_id):
    debug_id = (debug_id or "") + "-global"
    tg = graph_maker.make_graphs(
        toolchain,
        [os.path.join(opts.arc_root, t) for t in targets],
        graph_kind=_GraphKind.GLOBAL_TOOLS,
        debug_id=debug_id,
        enabled_events='PSLGE',
    )
    no_pic = tg.no_pic
    graph = no_pic().graph
    extern_resources = {}
    prefix_map = {i[: -len('_RESOURCE_GLOBAL')] + '-': i for i in resources if i.endswith('_RESOURCE_GLOBAL')}
    tool_resources = graph.get_resources()
    for resource in tool_resources:
        for p, r in prefix_map.items():
            if resource['pattern'].startswith(p):
                extern_resources[r] = '$({})'.format(resource['pattern'])
    return extern_resources, tool_resources


def _merge_target_graphs(
    graph_maker: _GraphMaker,
    conf_error_reporter: _ConfErrorReporter,
    opts,
    any_tests,
    graph_tools,
    target_graph: _TargetGraphsResult,
    graph_handles_num: int,
) -> _MergeTargetGraphResult:
    # merge target graphs for single target platform: tools + pic + no_pic

    with stager.scope('merge-target-graphs-{}'.format(graph_handles_num)):
        tpc = target_graph.target_tc
        tp_desc = _get_target_platform_descriptor(tpc, opts)
        tr = exts.timer.Timer('merge_target_graphs')

        with stager.scope('wait-target-graphs-{}'.format(graph_handles_num)):
            pic_tg = target_graph.pic()
            no_pic_tg = target_graph.no_pic()
            pic, mf_pic = pic_tg.graph, pic_tg.make_files_map
            no_pic, no_pic_tests, mf_no_pic = no_pic_tg.graph, no_pic_tg.tc_tests, no_pic_tg.make_files_map
            tr.show_step('waiting_graphs for {}'.format(tp_desc))

        with stager.scope('union-make-files-{}'.format(graph_handles_num)):
            make_files = bml.union_make_files(mf_pic, mf_no_pic)
            tr.show_step('union make files for {}'.format(tp_desc))

        with stager.scope('merge-graphs-{}'.format(graph_handles_num)):
            clang_tidy_mode = strtobool(opts.flags.get('TIDY', '0')) or strtobool(tpc.get('flags', {}).get('TIDY', '0'))
            graph = ccgraph.merge_graphs(graph_tools, pic, no_pic, conf_error_reporter)
            tr.show_step('merge_graphs for {}'.format(tp_desc))
            logger.debug('Graph size is %s after merge_graphs', graph.size())

        with stager.scope('strip-graph-{}'.format(graph_handles_num)):
            graph.strip()
            tr.show_step('strip_graph for {}'.format(tp_desc))
            logger.debug('Graph size is %s after strip_graph', graph.size())

        if clang_tidy_mode or any_tests and opts.drop_graph_result_before_tests:
            # Scrub result to avoid building irrelevant targets from RECURSE.
            # All required results will be added later while injecting tests.
            logger.debug("Drop graph's result (%d)", len(graph.get_result()))
            graph.set_result([])

        if _should_run_tests(opts, tpc):
            debug_id = 'test_tool_tc%s-{ispic}' % graph_handles_num
            extern_resources, test_tool_resources = _resolve_test_tools(graph_maker, tpc, debug_id, opts)
            if tconst.TEST_TOOL_HOST in extern_resources:
                graph.add_resources(test_tool_resources)

            for test in no_pic_tests:
                if tconst.TEST_TOOL_HOST in extern_resources:
                    test.global_resources.update(extern_resources)
            test_bundle = (no_pic_tests, tpc)
        else:
            test_bundle = None

        # TODO: mark node as sandboxing node during its creation
        if tpc.get('flags', {}).get('SANDBOXING') == 'yes':
            graph.add_sandboxing_mark()

        with stager.scope('get-graph-{}'.format(graph_handles_num)):
            # Get graph's python representation
            graph_dict = graph.get()

        return _MergeTargetGraphResult(graph_dict, test_bundle, make_files)


def _get_tools_from_suites(suites, ytexec_required):
    tools_from_suites = set()
    for suite in suites:
        if suite.special_runner == 'yt' and ytexec_required:
            tools_from_suites.add("ytexec")
        tools_from_suites.update(suite.get_resource_tools())
    return tools_from_suites


def _build_merged_graph(
    host_tool_resolver: _HostToolResolver,
    conf_error_reporter: _ConfErrorReporter,
    opts,
    print_status,
    src_dir,
    any_tests,
    target_tcs,
    merged_target_graphs: list[_MergeTargetGraphResult],
):
    merged_graph: graph_descr.DictGraph | None = None
    ytexec_required = False
    stripped_tests = []
    injected_tests = []
    sandboxing_test_uids = set()

    if any_tests:
        test_opts = _get_test_opts(opts)
    else:
        test_opts = None

    united_make_files = []
    for target_graph_num, target_graph in enumerate(merged_target_graphs, start=1):
        graph, test_bundle, make_files_by_platform = (
            target_graph.graph,
            target_graph.test_bundle,
            target_graph.make_files,
        )
        graph: graph_descr.DictGraph

        if test_bundle:
            tests, tpc = test_bundle
            tpc_test_opts = _get_tpc_test_opts(test_opts, tpc)

            with stager.scope('insert-tests-{}'.format(target_graph_num)):
                requested, stripped = _inject_tests(
                    opts, print_status, src_dir, conf_error_reporter, graph, tests, tpc, tpc_test_opts
                )
                injected_tests += requested
                stripped_tests += stripped

                if tpc.get('flags', {}).get('SANDBOXING') == 'yes':
                    sandboxing_test_uids |= {test.uid for test in requested}
                ytexec_required |= tpc_test_opts.run_tagged_tests_on_yt

        with stager.scope('merge-target-graphs-{}'.format(target_graph_num)):
            merged_graph: graph_descr.DictGraph = _naive_merge(merged_graph, graph)

        united_make_files = bml.union_make_files(united_make_files, make_files_by_platform)

    test_scope = injected_tests + stripped_tests

    if any_tests:
        _inject_tests_result_node(src_dir, merged_graph, test_scope, test_opts)

        tools_from_suites = _get_tools_from_suites(injected_tests, ytexec_required)
        resources = [host_tool_resolver.resolve(tool, tool.upper()) for tool in tools_from_suites]
        merged_graph['conf']['resources'].extend(resources)

        merged_graph['conf']['resources'].append(host_tool_resolver.resolve('python', 'PYTHON'))
        try:
            merged_graph['conf']['resources'].append(host_tool_resolver.resolve('gdb', 'GDB'))
        except Exception as e:
            logger.debug("Gdb will not be available for tests: %s", e)

        if getattr(opts, 'dlv'):
            try:
                merged_graph['conf']['resources'].append(host_tool_resolver.resolve('dlv', 'DLV'))
            # XXX
            except Exception as e:
                logger.debug("Dlv will not be available for tests: %s", e)

        if any(tc for tc in target_tcs if 'valgrind' in gen_plan.real_build_type(tc, opts)):
            try:
                merged_graph['conf']['resources'].append(host_tool_resolver.resolve('valgrind', 'VALGRIND'))
            except Exception as e:
                logger.debug("Valgrind will not be available for tests: %s", e)

    # TODO: mark node as sandboxing node during its creation
    if sandboxing_test_uids:
        for n in merged_graph['graph']:
            if n['uid'] in sandboxing_test_uids:
                n['sandboxing'] = True

    if opts.use_distbuild:
        conf = gen_plan.gen_extra_dict_by_opts(opts, repository_type=opts.repository_type)
        merged_graph['conf'].update(conf)

    return merged_graph, list(injected_tests), list(stripped_tests), united_make_files


def _inject_tests(opts, print_status, src_dir, conf_error_reporter, graph, tests, tpc, test_opts):
    assert test_opts is not None
    import devtools.ya.test.filter as test_filter
    import devtools.ya.test.test_node
    import devtools.ya.build.build_plan

    timer = exts.timer.Timer('inject_tests')
    print_status("Configuring tests execution")

    tests = test_filter.filter_suites(tests, test_opts, tpc)
    if test_opts.last_failed_tests and not test_opts.tests_filters:
        tests = devtools.ya.test.test_node.filter_last_failed(tests, opts)

    logger.debug("Generating build plan")
    plan = devtools.ya.build.build_plan.BuildPlan(graph)

    logger.debug("Preparing test suites")
    test_framer = devtools.ya.test.test_node.TestFramer(src_dir, plan, tpc, conf_error_reporter, test_opts)
    tests = test_framer.prepare_suites(tests)

    logger.debug("Stripping clang-tidy irrelevant deps")
    _clang_tidy_strip_deps(plan, tests)

    # split after filtering
    requested, stripped = _split_stripped_tests(tests, test_opts)
    # Some tests may have unmet dependencies, so they won't be injected into the graph.
    # Thus the returned set can be lesser than input one. (See DEVTOOLS-6384 for additional details).
    requested = devtools.ya.test.test_node.inject_tests(src_dir, plan, requested, test_opts, tpc)
    timer.show_step('inject tests for {}'.format(_get_target_platform_descriptor(tpc, opts)))
    logger.debug('injected %s tests for %s', len(requested), _get_target_platform_descriptor(tpc, opts))

    return requested, stripped


def _split_stripped_tests(tests, opts):
    import devtools.ya.test.test_node

    return devtools.ya.test.test_node.split_stripped_tests(tests, opts)


def _inject_tests_result_node(src_dir, graph: graph_descr.DictGraph, tests, opts) -> None:
    import devtools.ya.test.test_node
    import devtools.ya.build.build_plan

    buildplan = devtools.ya.build.build_plan.BuildPlan(graph)

    if opts.strip_idle_build_results:
        _strip_idle_build_results(graph, buildplan, tests)
    if opts.strip_skipped_test_deps:
        _strip_skipped_results(graph, buildplan, tests)

    devtools.ya.test.test_node.inject_tests_result_node(src_dir, buildplan, tests, opts)


def _strip_idle_build_results(graph, plan, tests):
    # Remove all result nodes (including build nodes) that are not required for tests run
    strip_idle_build_results_stage = stager.start('tests_strip_idle_build_results')

    results = set(graph['result'])
    required = set()
    for suite in tests:
        required.add(suite.uid)
        if not suite.is_skipped():
            required.update(suite.get_build_dep_uids())

    required = required & results
    seen = {}

    def test_dependent(uid):
        if uid in seen:
            return seen[uid]
        seen[uid] = False

        node = plan.get_node_by_uid(uid)
        # XXX Early stopping case to avoid traversing entire graph
        # Test node doesn't contain target_properties
        if not node.get("node-type") in [
            test_consts.NodeType.TEST,
            test_consts.NodeType.TEST_RESULTS,
            test_consts.NodeType.TEST_AUX,
        ]:
            return False

        deps = set(node.get("deps", []))
        if required & deps:
            seen[uid] = True
            return True

        for u in deps:
            if test_dependent(u):
                seen[uid] = True
                return True
        return False

    unused = results - required
    # retain results that depends on test nodes
    for uid in unused:
        if test_dependent(uid):
            required.add(uid)

    unused = results - required

    logger.debug("Removed %d nodes from results as unused by injected tests", len(unused))
    graph['result'] = list(required)

    strip_idle_build_results_stage.finish()


def _strip_skipped_results(graph, plan, tests):
    # remove test binaries from results for stripped test nodes
    # to avoid building targets which wouldn't be used
    tests_strip_skipped_results_stage = stager.start('tests_strip_skipped_results')

    results = graph['result']
    required, stripped = [], []
    for suite in tests:
        if suite.is_skipped():
            # stripped suites doesn't contain filled deps, we need to calculate them manually
            suite.fill_test_build_deps(plan)
            stripped += list(suite.get_build_dep_uids())
        else:
            required += list(suite.get_build_dep_uids())

    unused = set(stripped) - set(required)

    logger.debug("Going to remove %d nodes from results as unused by stripped tests", len(unused))
    graph['result'] = list(set(results) - unused)

    tests_strip_skipped_results_stage.finish()


def _get_test_opts(opts):
    import devtools.ya.test.filter as test_filter

    test_opts = copy.deepcopy(opts)
    project_path_filters = test_filter.get_project_path_filters(test_opts)
    # XXX modifying opts
    test_opts.tests_filters = [f for f in test_opts.tests_filters if f not in project_path_filters]
    test_opts.tests_path_filters = project_path_filters
    return test_opts


def _get_tpc_test_opts(opts, tpc):
    new_vals = {}

    # propagate target platform flags
    if tpc.get('flags'):
        new_vals['flags'] = dict(tpc['flags'])
        new_vals['flags'].update(opts.flags)

    if (
        opts.use_distbuild
        or strtobool(opts.flags.get('RUN_TAGGED_TESTS_ON_YT', '0'))
        or strtobool(tpc.get('flags', {}).get('RUN_TAGGED_TESTS_ON_YT', '0'))
    ):
        new_vals['run_tagged_tests_on_yt'] = True

    if new_vals:
        test_opts = copy.deepcopy(opts)
        for k, v in new_vals.items():
            setattr(test_opts, k, v)
        return test_opts
    return opts


def _prepare_for_ya_ide_idea(graph_maker, opts, ev_listener, first_target_graph, graph):
    import devtools.ya.jbuild.gen.gen as jb

    pic, no_pic, target_tc = first_target_graph.pic, first_target_graph.no_pic, first_target_graph.target_tc
    java_darts = pic().java_darts + no_pic().java_darts
    # test tool is required for DATA(sbr://) downloading
    extern_resources, test_tool_resources = _resolve_test_tool_only(
        graph_maker, target_tc, 'test_tool_only-{ispic}', opts
    )
    jgraph, _, ctx = jb.gen_jbuild_graph(
        opts.arc_root,
        java_darts,
        opts,
        cpp_graph=graph,
        ev_listener=ev_listener,
        extern_global_resources=extern_resources,
    )
    if jgraph["graph"]:
        jgraph = _add_resources(test_tool_resources, jgraph)
        graph = _naive_merge(jgraph, graph)
    return graph, ctx


def _strip_graph_results(
    graph: graph_descr.DictGraph, node_checker: tp.Callable[[graph_descr.GraphNode], bool], node_type_name: str
) -> graph_descr.DictGraph:
    results = set(graph['result'])
    stripped = {node['uid'] for node in graph['graph'] if node['uid'] in results and node_checker(node)}
    logger.debug("Going to remove %d nodes from results as %s node results", len(stripped), node_type_name)
    graph['result'] = list(results - stripped)
    return graph


def build_lite_graph(graph):
    lite_graph = {'inputs': graph.get('inputs', {}), 'result': graph['result'], 'graph': [], 'conf': graph['conf']}

    for node in graph['graph']:
        lite_node = {}
        for key in [
            'deps',
            'self_uid',
            'uid',
            'target_properties',
            'node-type',
            'tags',
            'java_tags',
            'inputs',
            'outputs',
            'kv',
            'host_platform',
            'tared_outputs',
        ]:
            if key in node:
                lite_node[key] = node[key]
        lite_graph['graph'].append(lite_node)

    return lite_graph


def get_version_info(arc_root, bld_root, outer_file=None, flags=None, custom_version='', release_version=''):
    flags = flags or {}
    logger.debug('Collect vcs info')
    timer = exts.timer.Timer('get_version_info')
    fake_data = strtobool(flags.get('NO_VCS_DEPENDS', 'no'))
    fake_build_info = strtobool(flags.get('CONSISTENT_BUILD', 'no'))
    if outer_file:
        with open(outer_file) as f:
            json_str = f.read()
    else:
        json_str = vcsversion.get_version_info(
            arc_root,
            bld_root,
            fake_data,
            fake_build_info,
            custom_version=custom_version,
            release_version=release_version,
        )
    logger.debug("Got version json  %s", json.dumps(json_str, sort_keys=True))

    if not fake_data:
        report_json = json.loads(json_str)
        report_json.pop('PROGRAM_VERSION', None)
        report_json.pop('SCM_DATA', None)
        report_json.pop('BUILD_DATE', None)
        report_json.pop('BUILD_TIMESTAMP', None)
        devtools.ya.core.report.telemetry.report(devtools.ya.core.report.ReportTypes.VCS_INFO, report_json)
        yalibrary.debug_store.dump_debug['vcs_info'] = report_json

    timer.show_step('vcs info')
    return {
        'resource': 'base64:vcs.json:{}'.format(six.ensure_str(base64.b64encode(six.ensure_binary(json_str)))),
        'pattern': 'VCS',
        'name': 'vcs',
    }
