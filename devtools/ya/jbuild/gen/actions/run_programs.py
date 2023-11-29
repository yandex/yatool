import os

from . import parse
import jbuild.gen.actions.compile as compile
import jbuild.gen.base as base
import jbuild.gen.node as node
import jbuild.gen.makelist_parser2 as mp
import jbuild.commands as commands
import jbuild.gen.consts as consts


def raise_empty(words):
    raise mp.ParseError('nothing to run in {}({})'.format(consts.RUN, ' '.join(words)))


def parse_words(words):
    cwd, ws = parse.extract_word(words, consts.R_CWD)
    add_srcs, ws = parse.extract_flag(ws, consts.R_ADD_SRCS)

    kv = parse.extract_words(
        ws,
        {
            consts.R_IN,
            consts.R_IN_DIR,
            consts.R_OUT,
            consts.R_OUT_DIR,
            consts.R_CLASSPATH,
            consts.R_CP_USE_COMMAND_FILE,
        },
    )

    ws = kv[None]

    return ws, cwd, add_srcs, kv


def strip_root(s):
    return s[3:]


def run_programs(path, target, ctx):
    module_cp = [x for x in ctx.classpath(path) if x != target.output_jar_path()]
    for words in target.plain.get(consts.RUN_MANAGED, []):
        args, cwd, add_srcs, kv = parse_words(words)

        if not args:
            raise_empty(words)

        old_args = args[:]
        args = []
        for arg in old_args:
            if arg == consts.MODULE_CLASSPATH:
                args += ['--fix-path-sep', '::'.join(module_cp)]
            else:
                args.append(arg)

        if cwd and not node.is_resolved(cwd):
            for p in node.iter_possible_outputs(consts.BUILD_ROOT, path, cwd):
                cwd = p

                break

        has_missing_paths = False
        for x in kv[consts.R_CLASSPATH]:
            x_path = strip_root(x)
            if x_path not in ctx.by_path:
                ctx.errs[path].missing_tools.append(x_path)
                has_missing_paths = True

        if has_missing_paths:
            continue

        # classpath
        cp = ctx.filtered_classpath(kv[consts.R_CLASSPATH], consts.CLS)
        if add_srcs:
            src_cp = ctx.filtered_classpath(kv[consts.R_CLASSPATH], consts.SRC)
            cp += src_cp

        dlls = ctx.classpath_dlls(kv[consts.R_CLASSPATH])
        libpath = [os.path.dirname(x) for x in dlls]

        if getattr(ctx.opts, 'be_verbose'):
            import sys

            sys.stderr.write(
                '=' * 25
                + '\n'
                + consts.RUN
                + '(\n'
                + '\n'.join(['    ' + x for x in words])
                + '\n)\n'
                + '\n'.join(cp)
                + '\n'
            )
        use_command_file = kv.get(consts.R_CP_USE_COMMAND_FILE, ['no'])[0].lower() == 'yes'
        manifest_name = '{}/bfg.jar'.format(consts.BUILD_ROOT)
        bf_name = '{}/bfg.txt'.format(consts.BUILD_ROOT)
        cmds = compile.make_build_file(list(map(compile.prepare_path_to_manifest, cp)), '\n', bf_name)
        if not use_command_file:
            cmds.append(commands.make_manifest_from_buildfile(bf_name, manifest_name))
            cp_name = manifest_name
        else:
            cp_name = '@' + bf_name
        cmds.append(
            commands.run_jp(
                [cp_name],
                args,
                base.resolve_jdk(
                    ctx.global_resources,
                    prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'),
                    opts=ctx.opts,
                ),
                libpath,
                cwd,
            )
        )

        yield node.JNode(
            path,
            cmds,
            node.files(kv[consts.R_IN] + cp + dlls) + node.dirs(kv[consts.R_IN_DIR]),
            node.files(kv[consts.R_OUT]) + node.dirs(kv[consts.R_OUT_DIR]),
            tag='RUN',
            res=False,
            fake_id=target.fake_id(),
        )
