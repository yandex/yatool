import collections
import itertools

import six

import exts.yjson as json
import logging
import operator
import os

import build.build_facade
import core.config

logger = logging.getLogger(__name__)


_Edge = collections.namedtuple('Edge', ['to', 'type'])


def _merge_results(results, addendum, split_by_types):
    if split_by_types:
        for k in addendum:
            if k not in results:
                results[k] = {}
            for t in addendum[k]:
                results[k][t] = sorted(set(results[k].get(t, [])) | set(addendum[k][t]))
    else:
        for k in addendum:
            results[k] = sorted(set(results.get(k, [])) | set(addendum[k]))


def load_dir_graph(graph, split_by_types=False):
    def type_miner(tp):
        if tp == 'Directory::Include::Directory':
            return 'RECURSE'
        return 'INCLUDE'

    jgraph = json.loads(graph)
    res = collections.defaultdict(dict)
    for subtarget in jgraph['graph']:
        deps_map = _walk(subtarget)
        _condense(deps_map)
        _merge_results(res, _prepare(deps_map, type_miner if split_by_types else None), split_by_types)

    return res


def find_path(graph, start, finish):
    used = set()
    queue = []
    previous_nodes = {}

    used.add(start)
    queue.append(start)
    while len(queue) != 0:
        v = queue.pop()
        for edge in graph.get(v, []):
            u = edge.to
            if u not in used:
                previous_nodes[u] = v
                queue.append(u)
                used.add(u)

    if finish not in previous_nodes:
        return None

    path = [finish]
    while path[-1] != start:
        path.append(previous_nodes[path[-1]])

    return list(reversed(path))


def gen_dir_graph(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    ymake_bin=None,
    host_platform=None,
    target_platforms=None,
    split_by_types=False,
):
    res = build.build_facade.gen_json_graph(
        build_root=build_root,
        build_type=build_type,
        build_targets=build_targets,
        debug_options=debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )
    return load_dir_graph(res.stdout, split_by_types)


def reachable(deps_map, start_node, split_by_types=False):
    def doit(start_nodes, selector):
        visited = set(start_nodes)
        q = collections.deque(start_nodes)

        while len(q) > 0:
            node = q.pop()
            pointed = deps_map.get(node)
            if pointed:
                pointed = selector(pointed)
            if pointed:
                for x in pointed:
                    if x not in visited:
                        visited.add(x)
                        q.append(x)

        return sorted(visited)

    if split_by_types:
        recs = doit([start_node], lambda x: x.get('RECURSE'))
        return doit(recs, lambda x: x.get('INCLUDE'))
    else:
        return doit([start_node], lambda x: x)


def _prepare(deps_map, type_miner):
    def fix(path):
        if path.startswith('$S/'):
            return path.replace('$S/', '')
        if path.startswith('$S'):
            return path.replace('$S', '')
        if path.startswith('$L') and path.count('$') > 1:
            return fix('$' + path.split('$')[-1])
        if path.startswith('$B') or path.startswith('$Z'):
            return None
        return path

    def adj(k, edges):
        lst = list(set([fix(x.to) for x in edges]))
        if k in lst:
            lst.remove(k)
        return lst

    result = collections.defaultdict(dict)
    for k, edges in six.iteritems(deps_map):
        kf = fix(k)
        if kf is not None:
            if type_miner:
                def func(edge):
                    return type_miner(edge.type)

                splitted = itertools.groupby(sorted(edges, key=func), func)
                for type, typed_edges in splitted:
                    a = adj(kf, typed_edges)
                    if a:
                        result[kf][type] = a
            else:
                a = adj(kf, edges)
                if a:
                    result[kf] = a

    if '.' in result:
        del result['.']

    return result


def _strip(deps_map):
    for k in deps_map:
        remove = []
        for e in deps_map[k]:
            if e.to not in deps_map and not e.to.startswith('$S'):
                remove.append(e)
        for e in remove:
            deps_map[k].discard(e)


def _scc_path(deps_map):
    identified = set()
    stack = []
    index = {}
    boundaries = []

    def dfs(v):
        index[v] = len(stack)
        stack.append(v)
        boundaries.append(index[v])

        for w in deps_map.get(v, ()):
            if w.to.startswith('$S'):
                continue
            elif w.to not in index:
                for scc in dfs(w.to):
                    yield scc
            elif w.to not in identified:
                while index[w.to] < boundaries[-1]:
                    boundaries.pop()

        if boundaries[-1] == index[v]:
            boundaries.pop()
            scc = set(stack[index[v] :])
            del stack[index[v] :]
            identified.update(scc)
            yield scc

    for v in deps_map.keys():
        if v not in index:
            for scc in dfs(v):
                yield scc


def _merge(component, deps_map):
    deps = set()
    for v in component:
        deps |= deps_map[v]

    # resolve only once
    resolved = component.pop()
    deps_map[resolved] = deps
    _resolve(resolved, deps_map)

    for v in component:
        deps_map[v] = deps_map[resolved]


def _resolve(node, deps_map):
    if node not in deps_map:
        return

    src_deps = set()
    resolved = set()
    for e in deps_map.get(node, ()):
        if e in resolved or e.to.startswith('$S') or e.to not in deps_map:
            continue

        resolved.add(e)
        for dep in deps_map[e.to]:
            if dep.to.startswith('$S'):
                src_deps.add(dep)

    deps_map[node] |= src_deps
    deps_map[node] -= resolved


def _condense(deps_map):
    logger.debug('start condensing, %d nodes', len(deps_map))

    _strip(deps_map)

    loops = []
    for component in _scc_path(deps_map):
        # _scc_path yields components in topologically sorted order
        if len(component) == 1:
            _resolve(component.pop(), deps_map)
        else:
            loops.append(len(component))
            _merge(component, deps_map)

    logger.debug(
        'end condensing, %d nodes (%d components with loops, %s total targets in loops)',
        len(deps_map),
        len(loops),
        sum(loops),
    )


def _walk(start_node):
    deps_lst = set()

    def is_module_node_type(node_type):
        return node_type in {'Library', 'Program', 'Bundle', 'Test', 'Run'}

    def is_valid_dep(from_node_type, dep_type, to_node_type):
        if from_node_type == 'BuildCommand' and dep_type == 'Include' and to_node_type == 'MakeFile':
            return False
        if is_module_node_type(from_node_type) and is_module_node_type(to_node_type):
            return False
        if dep_type == 'Property':
            return False
        return True

    def fix(path):
        if path.startswith('$B'):
            return path
        elif path.startswith('$S'):
            real_name = path.replace('$S', core.config.find_root())
            if os.path.isdir(real_name):
                return path
            elif os.path.isfile(real_name):
                return os.path.dirname(path)
            else:
                logger.warn('Unknown path {}'.format(real_name))
        elif path.startswith('$L') and path.count('$') > 1:
            return fix('$' + path.split('$')[-1])
        else:
            return '$Z:' + path

    def probe(from_node, to_node):
        from_node_name = from_node['name']
        to_node_name = to_node['name']

        from_node_type = from_node['node-type']
        dep_type = to_node['dep-type']
        to_node_type = to_node['node-type']

        p1 = fix(from_node_name)
        p2 = fix(to_node_name)
        if p1 and p2 and is_valid_dep(from_node_type, dep_type, to_node_type):
            deps_lst.add((p1, p2, from_node_type + '::' + dep_type + '::' + to_node_type))

        return True

    def mine_recurses(node):
        def is_recurse_property(node):
            return node['dep-type'] == 'Property' and node['name'].split(':')[-1] in ('RECURSES=', 'TEST_RECURSES=')

        def is_depends_property(node):
            return node['dep-type'] == 'Property' and node['name'].split(':')[-1] == 'DEPENDS='

        result = []
        if node['node-type'] == 'Directory':
            mkf = next((x for x in node['deps'] if x['node-type'] == 'MakeFile'), None)
            if mkf is not None:
                result = [item for dep in mkf['deps'] if is_recurse_property(dep) for item in dep['deps']]
                list(map(lambda dep: operator.setitem(dep, 'dep-type', 'Include'), result))
        elif is_module_node_type(node['node-type']):
            result = [item for dep in node['deps'] if is_depends_property(dep) for item in dep['deps']]
            list(map(lambda dep: operator.setitem(dep, 'dep-type', 'BuildFrom'), result))
        return result

    logger.debug('start walking')

    q = collections.deque()
    q.append(start_node)
    while len(q) != 0:
        elem = q.pop()
        elem['deps'] += mine_recurses(elem)
        for dep in elem['deps']:
            if probe(elem, dep):
                q.append(dep)

    sorted_deps = sorted(deps_lst)

    deps_map = {}
    for k, v in itertools.groupby(sorted_deps, operator.itemgetter(0)):
        deps_map[k] = set([_Edge(type=x[2], to=x[1]) for x in v])

    logger.debug('end walking')

    return deps_map
