import os
import logging

import jbuild.commands as commands
import jbuild.gen.node as node
import jbuild.gen.consts as consts

from . import funcs

logger = logging.getLogger(__name__)

GET_DEPS_TAG = 'get-deps'


def get_deps(path, target, ctx):
    def collect_of_type(t, dest):
        this_jar = target.output_jar_path()

        cp = ctx.classpath(path, t)
        pierced_cp = [x for x in cp if x != this_jar]

        dlls = []
        if t == consts.CLS:
            dlls = ctx.dlls(path)

        return node.JNode(
            path,
            [commands.mkdir(dest)] + [commands.mv(p, os.path.basename(p), cwd=dest) for p in pierced_cp + dlls],
            node.files(pierced_cp + dlls),
            node.dirs([dest]),
            tag=GET_DEPS_TAG,
            res=path in ctx.paths,
            fake_id=target.fake_id(),
        )

    if path in ctx.paths:
        yield collect_of_type(consts.CLS, 'bin')

        if ctx.opts.dump_sources:
            yield collect_of_type(consts.SRC, 'src')


def get_deps_funcs(res, nodes, dest):
    yield funcs.mkdirp(dest)

    clear = set()

    for n in nodes:
        if n.tag == GET_DEPS_TAG:
            for r, o in zip(res.ok_nodes.get(n.uid, []), n.outs):
                jar_path = r['artifact']

                d = os.path.abspath(os.path.join(dest, os.path.basename(os.path.dirname(o[0]))))

                if d not in clear:
                    yield funcs.rm(d)
                    yield funcs.mkdirp(d)

                    clear.add(d)

                yield funcs.jarx(jar_path, d)

    def report():
        logger.info('Successfully dumped deps: %s', dest)

    yield report
