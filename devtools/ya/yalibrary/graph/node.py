import os
import collections

from core.imprint import imprint
from . import base
from . import const
from exts import hashing

from six import iteritems


class ParseError(Exception):
    mute = True


class Node(object):
    uid_prefix = ''

    def __init__(
        self,
        path,
        cmds,
        ins,
        outs,
        tared_outs=None,
        res=True,
        tag=None,
        use_ya_hash=True,
        resources=None,
        requirements=None,
        timeout=None,
        kv=None,
        fake_id=None,
        target_properties=None,
        env=None,
    ):
        cmd_inputs = sum([c.inputs for c in cmds], [])
        self.path = path
        self.cmds = cmds
        self.ins = ins + [(inp, False) for inp in cmd_inputs]  # [(path, is_dir), ...]
        self.outs = outs  # [(path, is_dir), ...]
        self.ext_resources = {i['uri']: i for c in cmds for i in c.resources}
        self.deps = []
        self.uid = None
        self.uids = {}
        self.res = res
        self.tag = tag
        self.use_ya_hash = use_ya_hash
        self.tared_outs = tared_outs or []
        self.resources = resources or []
        self.requirements = requirements or {}
        self.timeout = timeout
        self.kv = kv or {'show_out': True}
        self.fake_id = fake_id
        self.target_properties = target_properties or {}
        self.id = node_id(self)  # depends only on what node does, not on data node operates with
        self.env = env

    def is_dart_node(self):
        return True

    def calc_node_uids(self, arc_root):
        paths = set()
        ins_unresolved = list()

        for i in self.ins:
            if base.in_source(i[0]):
                p = i[0].replace(const.SOURCE_ROOT, arc_root)

                if os.path.exists(p):
                    paths.add(p)
                else:
                    ins_unresolved.append(i)

        # cmd.resources are accounted in cmd_args at this moment.

        hashes = imprint(*paths)

        u_i = hashing.fast_hash('#'.join(map(str, sorted(self.ins))))
        u_o = hashing.fast_hash('#'.join(map(str, sorted(self.outs))))
        u_t = hashing.fast_hash('#'.join(map(str, sorted(self.tared_outs))))
        u_d = hashing.fast_hash('#'.join(map(str, [d.uid for d in self.deps])))
        u_c = hashing.fast_hash('#'.join(map(str, [c.uid() for c in self.cmds])))
        u_reqs = hashing.fast_hash('#'.join(map(str, sorted(iteritems(self.requirements)))))

        u_src = []

        for i in self.ins:
            if base.in_source(i[0]):
                u_src.extend([i[0], hashes.get(i[0].replace(const.SOURCE_ROOT, arc_root), '')])

        u_src = hashing.fast_hash('#'.join(sorted(u_src)))

        u_env = hashing.fast_hash('#'.join(map(str, sorted(self.env)))) if self.env else ''

        self.uids = {
            'ins': u_i,
            'outs': u_o,
            'tared_outs': u_t,
            'deps': u_d,
            'cmds': u_c,
            'requirements': u_reqs,
            'src': u_src,
            'fake_id': self.fake_id or '',
            'env': u_env,
        }

        return ins_unresolved

    def calc_node_uid(self, arc_root):
        ins_unresolved = self.calc_node_uids(arc_root)
        self.uid = self.uid_prefix + hashing.fast_hash('#'.join([self.uids[k] for k in sorted(self.uids)]))
        return ins_unresolved

    def to_serializable(self, ctx=None):
        cmds = []

        for cmd in self.cmds:
            p = {'cmd_args': cmd.cmd}

            if cmd.cwd:
                p['cwd'] = cmd.cwd

            if cmd.env:
                p['env'] = cmd.env

            cmds.append(p)

        n = {
            'cmds': cmds,
            'deps': sorted(set([dep.uid for dep in self.deps])),
            'inputs': sorted(set([i[0] for i in self.ins])),
            'outputs': [o[0] for o in self.outs],
            'tared_outputs': [o[0] for o in self.tared_outs],
            'resources': [self.ext_resources[k] for k in sorted(self.ext_resources.keys())],
            'uid': self.uid,
            'uids': dict([(k, v) for (k, v) in iteritems(self.uids) if v]),
            'kv': self.kv,
        }

        if self.requirements:
            n['requirements'] = self.requirements

        n['target_properties'] = self.target_properties

        for r in self.resources:
            n['kv']['needs_resource' + r] = True

        if self.timeout:
            n['timeout'] = self.timeout

        if self.env:
            n['env'] = self.env

        return n


class YmakeGrapNodeWrap(Node):
    def __init__(self, path, node, graph):
        super(YmakeGrapNodeWrap, self).__init__(
            path,
            [],
            [],
            [(base.hacked_normpath(o), const.FILE) for o in node['outputs']],
            tared_outs=[(base.hacked_normpath(to), const.FILE) for to in node.get('tared_outputs', [])],
            res=False,
            tag=None,
            use_ya_hash=False,
            resources=None,
        )

        self.node = node
        self.graph = graph

    def is_dart_node(self):
        return False

    def calc_node_uid(self, arc_root):
        self.uid = self.node['uid']
        self.uids = {}
        return list()

    def to_serializable(self, ctx=None):
        return self.node


def node_id(n):
    u_i = hashing.fast_hash('#'.join(map(str, sorted(n.ins))))
    u_o = hashing.fast_hash('#'.join(map(str, sorted(n.outs))))
    u_t = hashing.fast_hash('#'.join(map(str, sorted(n.tared_outs))))
    u_c = hashing.fast_hash('#'.join(map(str, [c.uid() for c in n.cmds])))

    return hashing.fast_hash('#'.join([str(n.path), u_i, u_o, u_t, u_c]))


def calc_uids(arc_root, nodes):
    ins_unresolved = collections.defaultdict(list)

    def __ideps(node):
        if not node.is_dart_node():
            return []
        return node.deps

    def __calc_node_uid(n):
        unr = n.calc_node_uid(arc_root)

        if unr:
            ins_unresolved[n] = unr

    def loop_serializer(lst):
        import exts.yjson as json

        return json.dumps([x.to_serializable() for x in lst], indent=4, sort_keys=True)

    base.traverse(nodes, ideps=__ideps, after=__calc_node_uid, on_loop=on_loop_raise(loop_serializer))

    return ins_unresolved


def merge(nodes):  # assumes all outputs are resolved and all inputs in source root are resolved
    by_id = {n.id: n for n in nodes}
    nodes = [by_id[i] for i in base.uniq_first_case([n.id for n in nodes])]

    by_o = collections.defaultdict(list)
    deps = collections.defaultdict(set)

    ins_unresolved = collections.defaultdict(list)

    for n in nodes:
        for o in n.outs:
            if not base.in_build(o[0]):
                raise Exception('{} outputs to source root: {}'.format(n.path, o[0]))

            by_o[o].append(n)

    for n in nodes:
        for j, inp in enumerate(n.ins):
            if base.in_source(inp[0]):
                continue

            elif base.in_build(inp[0]):
                if inp in by_o:
                    for d in by_o[inp]:
                        if d not in deps[n]:
                            n.deps.append(d)

                else:
                    ins_unresolved[n].append(inp)

            else:
                for ri in base.iter_possible_inputs(const.BUILD_ROOT, n.path, inp[0]):
                    if (ri, inp[1]) in by_o:
                        for d in by_o[(ri, inp[1])]:
                            if d not in deps[n]:
                                n.deps.append(d)

                        n.ins[j] = (ri, inp[1])
                        n.cmds = [base.fix_cmd(cmd, {inp[0]: ri}) for cmd in n.cmds]

                        break

                else:
                    ins_unresolved[n].append(inp)

    return nodes, ins_unresolved


def on_loop_raise(loop_serializer=lambda x: x):
    def on_loop(stack, last):
        raise ParseError('Loop detected: {}'.format(loop_serializer(base.loop(stack, last))))

    return on_loop
