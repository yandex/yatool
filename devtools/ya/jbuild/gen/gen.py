import logging
import os

import yalibrary.graph.base as graph_base
import yalibrary.graph.node as graph_node
from devtools.ya.core import stage_tracer
from exts.strtobool import strtobool

from . import base
from . import configure
from . import consts
from . import node
from . import makelist_parser2 as mp
from .actions import fetch_test_data

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("jbuild")


def gen_ctx(arc_root, opts, cpp_graph=None, dart=None, extern_global_resources=None):
    assert cpp_graph is not None
    assert dart is not None

    global_resources = extern_global_resources or {}
    for info in dart:
        if '_GLOBAL_RESOURCES_' in info:
            global_resources.update(graph_base.parse_resources(info['_GLOBAL_RESOURCES_']))

    rc = []
    by_path = mp.obtain_targets_graph2(dart, cpp_graph)

    return base.Context(
        opts,
        arc_root,
        set(rc),
        by_path,
        global_resources,
    )


def iter_path_nodes(path, ctx):
    assert path in ctx.by_path

    t = ctx.by_path[path]
    yield graph_node.YmakeGrapNodeWrap(path, t.node, t.graph)
    if t.plain is not None:
        for nod in fetch_test_data.fetch_test_data(path, t, ctx):
            node.resolve_ins(ctx.arc_root, nod)
            node.resolve_outs(nod)
            yield nod


def iter_nodes(ctx):
    for p in ctx.by_path:
        try:
            for n in iter_path_nodes(p, ctx):
                t = ctx.by_path[p]
                if hasattr(t, 'bundle_name'):
                    if not n.kv:
                        n.kv = {}
                    n.kv['bundle_name'] = t.bundle_name

                    if 'VCS_INFO_DISABLE_CACHE__NO_UID__' in t.plain and strtobool(
                        t.plain['VCS_INFO_DISABLE_CACHE__NO_UID__'][0][0]
                    ):
                        n.kv['disable_cache'] = 'yes'
                yield n
        except mp.ParseError as e:
            raise mp.ParseError('{}: {}'.format(p, str(e)))


def iter_result(ctx, nodes):
    seen = set()

    if ctx.opts.get_deps:
        for n in nodes:
            if n.tag == consts.GET_DEPS_TAG and n not in seen:
                seen.add(n)

                yield n

    if ctx.opts.idea_project_root:
        import devtools.ya.jbuild.gen.actions.idea as idea

        idea_result, _ = idea.idea_results(ctx, nodes)

        for n in idea_result:
            if n not in seen:
                seen.add(n)

                yield n

    if not ctx.opts.get_deps:
        for n in nodes:
            if ctx.opts.idea_project_root and consts.IDEA_NODE in (n.kv or {}) and n not in seen:
                seen.add(n)
                yield n
                continue
            if ctx.opts.idea_project_root:
                continue
            if n.res and n not in seen:
                seen.add(n)
                yield n


def default_opts():
    import devtools.ya.core.yarg as yarg
    import devtools.ya.jbuild.jbuild_opts as jbuild_opts

    return yarg.merge_opts(jbuild_opts.jbuild_opts()).params()


def gen(
    arc_root,
    opts,
    cpp_graph=None,
    ev_listener=None,
    dart=None,
    extern_global_resources=None,
):
    with stager.scope('insert_java-gen_ctx'):
        ctx = gen_ctx(
            arc_root,
            opts,
            cpp_graph=cpp_graph,
            dart=dart,
            extern_global_resources=extern_global_resources,
        )

    insert_java_detect_unversioned = stager.start('insert_java-detect_unversioned')
    nodes, ins_unresolved = graph_node.merge(list(iter_nodes(ctx)))

    node.resolve_io_dirs(nodes, ctx)

    res = list(iter_result(ctx, nodes))

    achievable = set()
    graph_base.traverse(res, after=achievable.add)

    ins_unresolved_ = graph_node.calc_uids(arc_root, achievable)

    for n, ins in ins_unresolved_.items():
        ins_unresolved[n].extend(ins)

    for n, ins in ins_unresolved.items():
        if n in achievable:
            ctx.errs[n.path].missing_inputs.extend([x[0] for x in ins])
            ctx.errs[n.path].missing_inputs.extend(
                [
                    os.path.join(n.path, x[0])
                    for x in ins
                    if not graph_base.in_source(x[0]) and not graph_base.in_build(x[0])
                ]
            )  # For selective checkout from JAVA_SRCS

    for e in ctx.errs.values():
        e.missing_inputs = sorted(set(e.missing_inputs))
    insert_java_detect_unversioned.finish()

    with stager.scope('insert_java-dump_graph'):
        graph = [n.to_serializable(ctx) for n in achievable if n.is_dart_node()]
        result = [n.uid for n in res]

    task = {'graph': graph, 'result': result, 'conf': {}}
    task.pop('conf')

    with stager.scope('insert_java-report_missing'):
        if ev_listener:
            from .actions import missing_dirs

            for p in missing_dirs.iter_missing_java_paths(ctx.errs):
                p = p.replace('$(SOURCE_ROOT)', '$S').replace('$(BUILD_ROOT)', '$B')
                ev_listener({'_typename': 'JavaMissingDir', 'Dir': p})

            for path, err in ctx.errs.items():
                if path in ctx.by_path and ctx.by_path[path].output_jar_name():
                    jname = ctx.by_path[path].output_jar_name()
                    if jname.endswith('.jar'):
                        path = '/'.join(['$B', path, jname[:-4]])
                for msg, sub in err.get_colored_errors():
                    ev_listener(
                        {
                            'Sub': sub,
                            'Type': 'Error',
                            'Message': msg,
                            '_typename': 'NEvent.TDisplayMessage',
                            'Where': path,
                            'Mod': 'bad',
                        }
                    )

    if ctx.errs:
        if not opts.continue_on_fail:
            raise configure.ConfigureError('Configure error (use -k to proceed)')

    ctx.nodes = nodes
    return task, ctx, ctx.errs


def gen_build_graph(
    arc_root,
    dart,
    make_opts,
    cpp_graph=None,
    ev_listener=None,
    extern_global_resources=None,
):
    opts = default_opts()
    opts.__dict__.update(make_opts.__dict__)  # XXX
    task, ctx, errs = gen(
        arc_root,
        opts,
        cpp_graph=cpp_graph,
        ev_listener=ev_listener,
        dart=dart,
        extern_global_resources=extern_global_resources,
    )

    return task, errs, ctx
