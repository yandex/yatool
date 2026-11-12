"""Choose a small local YT warmup without changing the configured graph."""

from collections import Counter

import yalibrary.platform_matcher as pm


def host_is_local(host: str, current: dict[str, str]) -> bool:
    parsed = pm.parse_platform(host)
    return all(parsed[key].lower() == current[key].lower() for key in ('os', 'arch'))


def estimate_work(graph: dict, local_cache, yt_cache, *, clear_build: bool = False) -> dict[str, int]:
    """Count work at the local runner's cache frontier.

    The remaining DistBuild graph still contains dependencies available in YT.
    Content-UID hits discovered during execution can further reduce this estimate.
    """
    nodes = {node['uid']: node for node in graph['graph']}
    seen: set[str] = set()
    depth: dict[str, int] = {}
    layers: Counter[int] = Counter()
    cached_local = cached_yt = 0
    pending = [(uid, False) for uid in graph['result']]
    while pending:
        uid, expanded = pending.pop()
        node = nodes[uid]
        if expanded:
            level = 1 + max((depth[dep] for dep in node.get('deps', [])), default=0)
            depth[uid] = level
            layers[level] += 1
            continue
        if uid in seen:
            continue
        seen.add(uid)
        cacheable = not clear_build and node.get('cache', True)
        if cacheable and local_cache.has(uid):
            cached_local += 1
            depth[uid] = 0
        elif cacheable and yt_cache.fits(node) and yt_cache.has(uid):
            cached_yt += 1
            depth[uid] = 0
        else:
            pending.append((uid, True))
            pending.extend((dep, False) for dep in node.get('deps', []))
    return {
        'executable_nodes': sum(layers.values()),
        'local_cached_nodes': cached_local,
        'yt_cached_nodes': cached_yt,
        'depth': max(layers, default=0),
        'max_layer_width': max(layers.values(), default=0),
    }


def select_execution(
    graph: dict, opts, local_cache, yt_cache, *, full_graph: bool, tests: bool, host: str
) -> dict | None:
    """Return an opt-in decision; the caller changes only the executor afterwards."""
    per_core = getattr(opts, 'yt_local_max_nodes_per_core', 0)
    if not per_core:
        return None
    threads = opts.build_threads
    budget = getattr(opts, 'yt_local_cores', 0) or threads
    cores = min(threads, budget)
    configured = 'distbuild' if opts.use_distbuild else 'local'
    decision = {
        'schema_version': 1,
        'mode': configured,
        'reason': 'configured-local' if configured == 'local' else 'node-limit-exceeded',
        'graph_nodes': len(graph['graph']),
        'result_nodes': len(graph['result']),
        'build_threads': threads,
        'cores': cores,
        'nodes_per_core': per_core,
        'max_nodes': per_core * cores,
    }
    if not yt_cache or yt_cache.tag() != 'YT':
        decision['reason'] = 'yt-cache-unavailable'
        return decision
    if configured == 'distbuild':
        if yt_cache.readonly():
            decision['reason'] = 'yt-cache-read-only'
            return decision
        if not (opts.yt_replace_result or opts.dist_cache_evict_cached):
            decision['reason'] = 'not-a-yt-warmup'
            return decision
        if opts.yt_store_exclusive:
            decision['reason'] = 'yt-exclusive-mode'
            return decision
        if not full_graph:
            decision['reason'] = 'custom-graph'
            return decision
        if tests:
            decision['reason'] = 'tests-requested'
            return decision
        if not host_is_local(host, pm.current_platform()):
            decision['reason'] = 'incompatible-execution-host'
            return decision
        if not threads:
            decision['reason'] = 'configure-only'
            return decision
    decision.update(estimate_work(graph, local_cache, yt_cache, clear_build=opts.clear_build))
    if configured == 'distbuild' and decision['executable_nodes'] <= decision['max_nodes']:
        decision['mode'] = 'local'
        decision['reason'] = 'small-graph' if graph['result'] else 'nothing-to-build'
    return decision
