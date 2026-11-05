import collections

import exts.hashing as hashing
import devtools.ya.build.graph_description as graph_descr


class _NodeGen:
    def __init__(self, python3_pattern: str):
        self.extra_nodes: list[graph_descr.GraphNode] = []
        self._python3_pattern = python3_pattern

    @staticmethod
    def is_cacheable_node(node):
        return node.get('cache', not node.get('kv', {}).get('disable_cache', False))

    def gen_rename_node(self, node: graph_descr.GraphNode, suffix) -> graph_descr.GraphNodeUid:
        import devtools.ya.test.test_node.cmdline as cmdline

        inputs = node['outputs']
        outputs = [inp + suffix for inp in inputs]

        tared_outputs = [inp + suffix for inp in node.get('tared_outputs', [])]
        cmd_args = [arg for pair in zip(inputs, outputs) for arg in pair]

        ret = {
            'uid': hashing.md5_value(node['uid'] + '-' + suffix),
            'cmds': [
                {
                    'cmd_args': [
                        "$({})/bin/python3".format(self._python3_pattern),
                        '$(SOURCE_ROOT)/build/scripts/move.py',
                    ]
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
        self.extra_nodes.append(self.copy_platform_and_tags(node, ret))
        return ret['uid']

    def copy_platform_and_tags(self, fr, to):
        if tags := fr.get('tags', None):
            to['tags'] = tags
        if platform := fr.get('platform'):
            to['platform'] = platform
        if platform_id := fr.get('platform_id'):
            to['platform_id'] = platform_id
        return to


def rename_conflicting_results(graph: graph_descr.DictGraph, python3_pattern: str, uid_map):
    """Append copy nodes for result output collisions using resolved host Python."""
    node_gen = _NodeGen(python3_pattern)
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

            prefix = node.get('platform_id')
            if not prefix:
                # We would like to preserve resaonably-sized prefixes, but trim longer ones
                prefix = '-'.join(node.get('tags', [u]))
                if len(prefix) > 64:
                    prefix = '{}..{}'.format(prefix[:54], hashing.md5_value(prefix)[:8])

            yield node_gen.gen_rename_node(node, '.' + prefix)

    graph['result'] = [x for x in iter_results()]
    graph['graph'].extend(node_gen.extra_nodes)
    return graph
