import os
import collections

import devtools.ya.jbuild.gen.base as base
import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.commands as commands
from exts import hashing
import yalibrary.graph.base as graph_base
import yalibrary.graph.node as graph_node

DIR = True
FILE = False


class JNode(graph_node.Node):
    uid_prefix = 'JAVA_'

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
    ):
        modified_kv = {
            'p': 'JV',
            'pc': 'light-blue',
            'show_out': True,
        }

        if kv:
            modified_kv.update(kv)

        super(JNode, self).__init__(
            path,
            cmds,
            ins,
            outs,
            tared_outs,
            res,
            tag,
            use_ya_hash,
            resources,
            requirements,
            timeout,
            modified_kv,
            fake_id,
            target_properties,
        )

    def to_serializable(self, ctx=None):
        self.target_properties = {}

        sources_as_modules = ctx is not None and (
            getattr(ctx.opts, 'coverage', False) or getattr(ctx.opts, 'sonar', False)
        )

        is_mod = False

        for out, is_dir in self.outs:
            parts = graph_base.hacked_normpath(out).split('/')[1:]
            dirname, basename = '/'.join(parts[:-1]), parts[-1]

            if dirname == self.path:
                if basename.endswith('-sources.jar'):
                    if sources_as_modules:
                        is_mod = True

                elif basename.endswith('.jar'):
                    is_mod = True

        if is_mod:
            self.target_properties['is_module'] = True
            self.target_properties['module_type'] = 'JAR'
            self.target_properties['module_dir'] = self.path

        if self.tag == 'RUN':
            self.target_properties['run'] = True

        n = super(JNode, self).to_serializable(ctx)

        if self.tag:
            n['java_tags'] = [self.tag]

        n.update(
            {
                'type': 2,
                'broadcast': False,
                'priority': 0,
                'env': {
                    'YA_CACHE_DIR': consts.BUILD_ROOT,
                },
            }
        )

        return n


def files(lst):
    return [(graph_base.hacked_normpath(f), FILE) for f in lst]


def dirs(lst):
    return [(graph_base.hacked_normpath(f), DIR) for f in lst]


def is_resolved(path):
    return graph_base.in_source(path) or graph_base.in_build(path)


def iter_possible_outputs(root, srcdir, relpath):
    yield graph_base.hacked_path_join(root, srcdir, relpath)


def try_resolve_inp(arc_root, srcdir, path):
    for p in graph_base.iter_possible_inputs(consts.SOURCE_ROOT, srcdir, path):
        r = p.replace(consts.SOURCE_ROOT, arc_root)

        if os.path.exists(r) and os.path.isdir(r):
            return p

    return path


def try_resolve_inp_file(arc_root, srcdir, path):
    if graph_base.in_source(path):
        return path
    for p in graph_base.iter_possible_inputs(consts.SOURCE_ROOT, srcdir, path):
        r = p.replace(consts.SOURCE_ROOT, arc_root)

        if os.path.exists(r) and os.path.isfile(r):
            return p

    return path


def file(n, dir):
    return graph_base.hacked_path_join(dir, hashing.fast_hash(dir + n.id) + '.jar')


def resolve_io_dirs(nodes, ctx):
    by_o = collections.defaultdict(list)

    for n in nodes:
        for o in n.outs:
            by_o[o].append(n)

    for n in nodes:
        mkds = []
        jars = []
        mvs = []

        for j, o in enumerate(n.outs):
            if o[1]:
                jdk_resource = base.resolve_jdk(ctx.global_resources, opts=ctx.opts)
                f = file(n, o[0])
                b = graph_base.hacked_path_join(consts.BUILD_ROOT, os.path.basename(f))

                mkds.append(commands.mkdir(o[0]))
                jars.append(commands.jar(os.curdir, b, jdk_resource, cwd=o[0]))
                mvs.append(commands.mv(b, f))

                n.outs[j] = (f, FILE)

        jarxs = []

        for j, i in enumerate(n.ins):
            if i[1]:
                for d in by_o[i]:
                    jdk_resource = base.resolve_jdk(ctx.global_resources, opts=ctx.opts)
                    f = file(d, i[0])

                    n.ins[j] = (f, FILE)

                    jarxs.extend([commands.jarx(f, jdk_resource, cwd=i[0]), commands.rm(f)])

        n.cmds = jarxs + mkds + n.cmds + jars + mvs


def resolve_outs(node):
    outs = []

    for o in node.outs:
        if is_resolved(o[0]):
            outs.append(o)

        else:
            for ro in iter_possible_outputs(consts.BUILD_ROOT, node.path, o[0]):
                outs.append((ro, o[1]))

                break

    m = dict(zip([o[0] for o in node.outs], [o[0] for o in outs]))

    node.cmds = [graph_base.fix_cmd(cmd, m) for cmd in node.cmds]
    node.tared_outs = [(m.get(o[0], o[0]), o[1]) for o in node.tared_outs]
    node.outs = outs


def resolve_ins(arc_root, node):
    ins = []

    for inp in node.ins:
        if is_resolved(inp[0]):
            ins.append(inp)

        else:
            for ri in graph_base.iter_possible_inputs(consts.SOURCE_ROOT, node.path, inp[0]):
                real = ri.replace(consts.SOURCE_ROOT, arc_root)

                if os.path.exists(real) and os.path.isdir(real) == inp[1]:
                    ins.append((ri, inp[1]))

                    break

            else:
                ins.append(inp)

    m = dict(zip([i[0] for i in node.ins], [i[0] for i in ins]))

    node.cmds = [graph_base.fix_cmd(cmd, m) for cmd in node.cmds]
    node.ins = ins
