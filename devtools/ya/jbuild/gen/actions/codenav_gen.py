import os

import jbuild.commands as commands
import jbuild.gen.base as base
import jbuild.gen.consts as consts
import jbuild.gen.node as node

import yalibrary.graph.base as graph_base


def _have_kindex(target):
    return target.is_dart_target() and consts.JAVA_SRCS in target.plain


def entry_gen_node(path, kindex, entry, kythe_to_proto_tool, binding_only, jdk_resource, fake_id):
    return node.JNode(
        path,
        [
            commands.make_codenav_entry(
                kythe_to_proto_tool, [kindex], entry, binding_only, jdk_resource, cwd=consts.BUILD_ROOT
            )
        ],
        ins=node.files([kindex]),
        outs=node.files([entry]),
        resources=['KYTHETOPROTO'] if not kythe_to_proto_tool else [],
        res=False,
        timeout=30 * 60,
        fake_id=fake_id,
    )


def gen_codenav(path, ctx, kythe_to_proto_tool):
    assert path in ctx.by_path
    assert ctx.by_path[path].is_dart_target()

    target = ctx.by_path[path]
    jdk_resource = base.resolve_jdk(
        ctx.global_resources, prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'), opts=ctx.opts
    )
    if _have_kindex(target):
        kindex = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'kindex.tar')
        entry = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'entry.min.json')
        yield entry_gen_node(path, kindex, entry, kythe_to_proto_tool, True, jdk_resource, target.fake_id())
        if path in ctx.rclosure:
            entry = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'entry.json')
            yield entry_gen_node(path, kindex, entry, kythe_to_proto_tool, False, jdk_resource, target.fake_id())
        if path in ctx.rclosure:
            entries = {entry}
            result_entry = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'entry.full.json')
            for jar in ctx.classpath(path, consts.CLS):
                if jar.startswith(consts.BUILD_ROOT):
                    jar = jar[(len(consts.BUILD_ROOT) + 1) :]
                jar = os.path.dirname(jar)
                if jar in ctx.by_path and _have_kindex(ctx.by_path[jar]) and jar != path:
                    entry = graph_base.hacked_path_join(consts.BUILD_ROOT, jar, 'entry.min.json')
                    entries.add(entry)
            yield node.JNode(
                path,
                [commands.merge_files(result_entry, list(entries), cwd=consts.BUILD_ROOT)],
                node.files(list(entries)),
                node.files([result_entry]),
                res=False,
                fake_id=target.fake_id(),
            )
            bfg_path = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'bfg.txt')
            ydx_name = target.output_jar_path() + '.ydx.pb2'
            yield node.JNode(
                path,
                [commands.kythe_to_proto(result_entry, ydx_name, bfg_path, kythe_to_proto_tool, cwd=consts.BUILD_ROOT)],
                node.files([bfg_path, result_entry]),
                node.files([ydx_name]),
                resources=['KYTHETOPROTO'] if not kythe_to_proto_tool else [],
                res=True,
                timeout=60 * 60,
                fake_id=target.fake_id(),
            )
