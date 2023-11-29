import os

import jbuild.gen.base as base
import jbuild.gen.makelist_parser2 as mp
import jbuild.gen.node as node
import jbuild.commands as commands
import jbuild.gen.consts as consts
from . import parse


def determine_external_loc(external):
    if external.startswith('sbr:'):
        return consts.E_SANDBOX, external[4:]

    elif external.startswith('jdk:'):
        return consts.E_JDK, external[4:]

    else:
        return consts.E_LOCAL, external


def extract_ej(plain):
    classes, sources, wars, aars = [], [], [], []

    for words in plain.get(consts.EXTERNAL_JAR, []):
        kv = parse.extract_words(words, {consts.E_SOURCES, consts.E_WAR, consts.E_AAR})

        c, s, w, a = kv[None], kv[consts.E_SOURCES], kv[consts.E_WAR], kv[consts.E_AAR]

        classes.extend(c)
        sources.extend(s)
        wars.extend(w)
        aars.extend(a)

    return classes, sources, wars, aars


def extract_classes_jar(aar, dest, jdk_resource):
    unpacked_aar = aar + '.unpacked'
    return [
        commands.mkdir(unpacked_aar),
        commands.jar([unpacked_aar], 'classes.jar', jdk_resource, cwd=unpacked_aar),
        commands.jarx(aar, jdk_resource, False, unpacked_aar),
        commands.cp(os.path.join(unpacked_aar, 'classes.jar'), dest),
    ]


def externals(path, target, ctx):
    classes, sources, wars, aars = extract_ej(target.plain)

    if len(classes) > 1:
        raise mp.ParseError('too many classes in {}'.format(consts.EXTERNAL_JAR))

    if len(sources) > 1:
        raise mp.ParseError('too many sources in {}'.format(consts.EXTERNAL_JAR))

    if len(wars) > 1:
        raise mp.ParseError('too many wars in {}'.format(consts.EXTERNAL_JAR))

    if len(aars) > 1:
        raise mp.ParseError('too many aars in {}'.format(consts.EXTERNAL_JAR))

    jdk_resource = base.resolve_jdk(
        ctx.global_resources, prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'), opts=ctx.opts
    )

    exts = [(c, consts.CLS) for c in classes]
    exts += [(a, consts.AAR) for a in aars]
    exts += [(w, consts.WAR) for w in wars]
    exts += [(s, consts.SRC) for s in sources]

    for e, t in exts:
        loc, val = determine_external_loc(e)
        dest = target.output_jar_of_type_path(t)

        res = path in ctx.rclosure

        if t == consts.SRC:
            res &= ctx.opts.dump_sources
            res |= getattr(ctx.opts, 'coverage', False)
            res |= getattr(ctx.opts, 'sonar', False) and path in ctx.sonar_paths

        if loc == consts.E_LOCAL:
            cmds = [commands.cp(val, dest)]
            if t == consts.AAR:
                aar, dest = dest, target.output_jar_of_type_path(consts.CLS)
                cmds += extract_classes_jar(aar, dest, jdk_resource)

            yield node.JNode(
                path,
                cmds,
                [(val, node.FILE)],
                [(dest, node.FILE)],
                res=res,
                kv={'p': 'CP', 'pc': 'light-cyan'},
                fake_id=target.fake_id(),
            )

        elif loc == consts.E_JDK:
            yield node.JNode(
                path,
                [commands.cp(os.path.join(jdk_resource, val), dest)],
                [],
                [(dest, node.FILE)],
                res=res,
                kv={'p': 'CP', 'pc': 'light-cyan'},
                fake_id=target.fake_id(),
            )

        else:
            assert loc == consts.E_SANDBOX

            cmds = []
            cmds.append(
                commands.fetch_resource(
                    val,
                    dest,
                    custom_fetcher=getattr(ctx.opts, 'custom_fetcher', None),
                    verbose=getattr(ctx.opts, 'be_verbose', False),
                    cwd=consts.BUILD_ROOT,
                )
            )

            if t == consts.AAR:
                aar, dest = dest, target.output_jar_of_type_path(consts.CLS)
                cmds += extract_classes_jar(aar, dest, jdk_resource)

            yield node.JNode(
                path,
                cmds,
                [],
                [(dest, node.FILE)],
                requirements={"network": "full"},
                res=res,
                use_ya_hash=False,
                kv={'p': 'SB', 'pc': 'yellow'},
                fake_id=target.fake_id(),
            )
