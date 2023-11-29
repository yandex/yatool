import os
import logging
import collections

import jbuild.gen.base as base
import jbuild.gen.node as node
import jbuild.commands as commands
from . import parse
import jbuild.gen.makelist_parser2 as mp
import jbuild.gen.consts as consts
import yalibrary.graph.base as graph_base

logger = logging.getLogger(__name__)


def make_build_file(lst, sep, path, max_cmd_len=8000, max_path_prefix=100):
    cmds = [commands.mkdir(os.path.dirname(path)), commands.append(path, '')]

    content, content_length = [], 0
    for cmd in lst:
        cmd_real_length = len(cmd) + max_path_prefix
        if cmd_real_length > max_cmd_len:
            logger.error("Can't create build file. String %s is too long", cmd)
            raise Exception("%s is too long" % cmd)
        if content_length + cmd_real_length > max_cmd_len:
            content = sep.join(content)
            content += sep
            cmds.append(commands.append(path, content))
            content, content_length = [], 0
        content.append(cmd)
        content_length += cmd_real_length + len(sep)
    if content:
        cmds.append(commands.append(path, sep.join(content)))
    return cmds


def prepare_path_to_manifest(path):
    return path[13:].lstrip('/').lstrip('\\') if path.startswith('$(BUILD_ROOT)') else path


def iter_processors(plain):
    for ws in plain.get(consts.ANNOTATION_PROCESSOR, []):
        for w in ws:
            yield w


def empty_jar_cmds(dlls, out, jdk_resource, target, manifest):
    empty_dir = os.path.join(consts.BUILD_ROOT, 'empty')

    generate_vcs_info_cmds = commands.gen_vcs_info_cmds(target, os.curdir, manifest, cwd=empty_dir)

    return (
        [commands.mkdir(empty_dir)]
        + list(dlls_copy_cmds(dlls, os.curdir))
        + generate_vcs_info_cmds
        + [commands.jar(os.curdir, out, jdk_resource, manifest=manifest, cwd=empty_dir)]
    )


def collect_cmds(artifacts, tar_path):
    collect_dir = os.path.join(consts.BUILD_ROOT, 'collect_dir')

    return (
        [commands.mkdir(collect_dir)]
        + [commands.cp(jar, os.path.join(collect_dir, os.path.basename(jar))) for jar in artifacts]
        + [commands.tar(tar_path, '', cwd=collect_dir)]
    )


def parse_words(words):
    is_resource, ws = parse.extract_word(words, consts.J_RESOURCE)
    srcdir, ws = parse.extract_word(words, consts.J_SRCDIR)
    pp, ws = parse.extract_word(ws, consts.J_PACKAGE_PREFIX)
    e, ws = parse.extract_word(ws, consts.J_EXTERNAL)
    kv = parse.extract_words(
        ws,
        {
            consts.J_EXCLUDE,
        },
    )

    return is_resource, srcdir, pp, e, kv[None], kv[consts.J_EXCLUDE]


def parse_patterns(words, specword):
    if len(words) < 2:
        raise mp.ParseError(
            'expected [{} path pattern1 ... patternN], got [{} {}]'.format(specword, specword, ' '.join(words))
        )

    return words[0], words[1:]


def get_ya_make_flags(plain, flags_name):
    res = {}
    key = None
    if flags_name in plain:
        raw = sum(plain.get(flags_name, []), [])
        for item in raw:
            if not item.startswith('-') and key is None:
                raise mp.ParseError(
                    'Error parsing {}: {} not match (-key (value)?) pattern'.format(flags_name, ' '.join(raw))
                )
            if item.startswith('-'):
                key = item[1:]
                res[key] = None
            else:
                res[key] = item
                key = None
    return res


def compile(path, target, ctx):
    sources = []
    resources = []
    kotlins = []
    groovys = []
    srcdirs = []
    pps = []
    inputs = []
    need_resources = []
    source_files = []

    war_sources = []
    war_resources = []
    war_srcdirs = []
    war_pps = []
    war_inputs = []

    misc = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'misc')
    flags = get_ya_make_flags(target.plain, consts.JAVAC_FLAGS)
    flags.update(ctx.opts.javac_flags)
    processors = list(iter_processors(target.plain))

    jdk_resource = base.resolve_jdk(
        ctx.global_resources, prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'), opts=ctx.opts
    )

    if processors:
        flags['processor'] = ','.join(processors)

    with_kotlin = consts.WITH_KOTLIN in target.plain
    kotlin_jvm_target = target.plain.get(consts.KOTLIN_JVM_TARGET, [[None]])[0][0]
    kotlinc_flags = get_ya_make_flags(target.plain, consts.KOTLINC_FLAGS)
    kotlinc_opts = list(sum(target.plain.get(consts.KOTLINC_OPTS, []), []))
    with_groovy = consts.WITH_GROOVY in target.plain

    def is_war_srcs(srcdir):
        return os.path.basename(srcdir) == 'webapp'

    for i, words in enumerate(target.plain.get(consts.JAVA_SRCS, [])):
        _, srcdir, pp, extern, ws, exc = parse_words(words)

        sources_file = os.path.join(misc, str(i) + '-sources.txt')
        resources_file = os.path.join(misc, str(i) + '-resources.txt')
        kotlin_sources_file = os.path.join(misc, str(i) + '-kotlin-sources.txt')
        groovy_sources_file = os.path.join(misc, str(i) + '-groovy-sources.txt')
        pp_path = graph_base.hacked_path_join(*((pp or '').strip().split('.')))

        if extern:
            srcdir = graph_base.hacked_path_join(consts.BUILD_ROOT, path, str(i) + '-sources')

        else:
            if not srcdir:
                srcdir = graph_base.hacked_path_join(consts.SOURCE_ROOT, path)

            srcdir = node.try_resolve_inp(ctx.arc_root, path, srcdir)

        if graph_base.in_source(srcdir):
            target.my_sources.append('{}::{}'.format(sources_file, srcdir))
            srcs, ress, kts, grs = ctx.resolved_sources[path][(srcdir, pp)]
            ins = node.files([graph_base.hacked_path_join(srcdir, x) for x in srcs + ress + kts + grs])
            source_files += [graph_base.hacked_path_join(srcdir, x) for x in srcs]  # xxx should add kotlin files?

            def dump(lst, f):
                return node.JNode(
                    path,
                    make_build_file(lst, ' ', f),
                    ins=ins,
                    outs=node.files([f]),
                    res=False,
                    fake_id=target.fake_id(),
                )

            yield dump(srcs, sources_file)
            yield dump(ress, resources_file)
            yield dump((srcs + kts) if with_kotlin else [], kotlin_sources_file)
            yield dump((srcs + grs) if with_groovy else [], groovy_sources_file)

        else:
            ins = node.dirs([srcdir])
            if extern:
                srcs = ctx.by_path[extern].java_sources_paths()
                if with_kotlin:
                    srcs += ctx.by_path[extern].kotlin_sources_paths()

                yield node.JNode(
                    path,
                    [
                        commands.collect_java_srcs(graph_base.hacked_path_join(consts.BUILD_ROOT, extern), srcs, srcdir)
                    ],  # command jsrcs --> srcdir
                    ins=node.files(srcs),
                    outs=node.dirs([srcdir]),
                    res=False,
                    fake_id=target.fake_id(),
                )

            yield node.JNode(
                path,
                [
                    commands.resolve_java_srcs(
                        srcdir,
                        sources_file,
                        kotlin_sources_file,
                        groovy_sources_file,
                        resources_file,
                        ws,
                        exclude_patterns=exc,
                        resolve_kotlin=with_kotlin,
                    )
                ],
                ins=ins,
                outs=node.files([sources_file, resources_file, kotlin_sources_file, groovy_sources_file]),
                res=False,
                fake_id=target.fake_id(),
            )

        if is_war_srcs(srcdir):
            war_sources.append(sources_file)
            war_resources.append(resources_file)
            war_srcdirs.append(srcdir)
            war_inputs += ins + node.files([sources_file, resources_file])
            war_pps.append(pp_path)
        else:
            sources.append(sources_file)
            resources.append(resources_file)
            kotlins.append(kotlin_sources_file)
            groovys.append(groovy_sources_file)
            srcdirs.append(graph_base.hacked_normpath(srcdir))
            inputs += ins + node.files([sources_file, resources_file, kotlin_sources_file, groovy_sources_file])
            pps.append(pp_path)

    src = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'src')
    src_jar = target.output_sources_jar_name()

    is_java_program = consts.JAVA_PROGRAM in target.plain
    is_uberjar = consts.UBERJAR in target.plain
    javac_generated_srcs_dir = target.plain.get(consts.SAVE_JAVAC_GENERATED_SRCS_DIR)
    javac_generated_srcs_tar = target.plain.get(consts.SAVE_JAVAC_GENERATED_SRCS_TAR)
    add_jdk_resource = consts.WITH_JDK in target.plain
    this_jar = target.output_jar_path()
    this_src_jar = target.output_sources_jar_path()
    this_war = target.output_war_path()

    manifest = target.output_vcs_mf_path() if 'EMBED_VCS' in target.plain else None

    if consts.JAVA_SRCS in target.plain:
        tar_path = target.output_sources_jar_path()[:-4] + '.tar'
        generate_vcs_info_cmds = commands.gen_vcs_info_cmds(target, os.curdir, manifest, cwd=src)

        cmds = (
            [commands.mkdir(src)]
            + [
                commands.copy_files(f, s, os.path.join(src, p) if p else src)
                for f, s, p in zip(sources + resources + kotlins + groovys, srcdirs * 4, pps * 4)
            ]
            + generate_vcs_info_cmds
            + [commands.jar(os.curdir, src_jar, jdk_resource, manifest=manifest, cwd=src)]
        )
        ins = inputs[:]
        outs = node.files([src_jar])
        tared_outs = []

        if is_java_program and not is_uberjar:
            cp = ctx.classpath(path, consts.SRC)
            cmds += collect_cmds(cp, tar_path)
            ins += node.files([x for x in cp if x != this_src_jar])
            outs += node.files([tar_path])
            tared_outs = node.files([tar_path])

        yield node.JNode(
            path,
            cmds,
            ins,
            outs,
            tared_outs=tared_outs,
            res=(
                (path in ctx.rclosure and ctx.opts.dump_sources)
                or getattr(ctx.opts, "java_coverage", False)
                or getattr(ctx.opts, "coverage", False)
                or (getattr(ctx.opts, 'sonar', False) and path in ctx.sonar_paths)
            ),
            fake_id=target.fake_id(),
        )

    cls = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'cls')
    cls_jar = target.output_jar_name()
    kotlin_cls = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'kotlin-cls')
    groovy_cls = graph_base.hacked_path_join(consts.BUILD_ROOT, path, 'groovy-cls')

    tar_path = target.output_jar_path()[:-4] + '.tar'

    bf = os.path.join(misc, 'bf.txt')
    kotlin_bf = os.path.join(misc, 'kotlin-bf.txt')
    groovy_bf = os.path.join(misc, 'groovy-bf.txt')

    if consts.JAVA_SRCS in target.plain:
        error_prone_resource = (
            base.resolve_error_prone(ctx.global_resources, ctx.opts) if consts.ERROR_PRONE in target.plain else None
        )
        kotlin_compiler_resource = base.resolve_kotlin_compiler(ctx.global_resources, ctx.opts) if with_kotlin else None
        groovy_compiler_resource = base.resolve_groovy_compiler(ctx.global_resources, ctx.opts) if with_groovy else None
        cp = ctx.classpath(path, consts.CLS)
        wars = ctx.wars(path)
        dlls = ctx.dlls(path)
        pierced_cp = [x for x in cp if x != this_jar]
        compile_cp = (
            pierced_cp
            if consts.DIRECT_DEPS_ONLY not in target.plain
            else [x for x in ctx.classpath(path, consts.CLS, direct=True) if x != this_jar]
        )
        copy_dll_cmds = list(dlls_copy_cmds(dlls, cls)) if consts.ADD_DLLS_FROM_DEPENDS in target.plain else []
        generate_vcs_info_cmds = commands.gen_vcs_info_cmds(target, os.curdir, manifest, cwd=cls)

        cmds = [
            commands.mkdir(cls),
            commands.mkdir(os.path.dirname(bf)),
            commands.prepare_build_file(sources, srcdirs, bf),
        ]
        if with_kotlin:
            cmds += [
                commands.mkdir(kotlin_cls),
                commands.mkdir(os.path.dirname(kotlin_bf)),
                commands.prepare_build_file(kotlins, srcdirs, kotlin_bf),
            ]
            cmds += commands.kotlinc(
                kotlin_bf,
                jdk_resource,
                kotlin_compiler_resource,
                module_name=os.path.basename(target.output_jar_path()[:-4]),
                deps=pierced_cp,
                out_dir=kotlin_cls,
                jvm_target=kotlin_jvm_target,
                cwd=consts.BUILD_ROOT,
                custom_flags=kotlinc_flags,
                custom_opts=kotlinc_opts,
            )
        if with_groovy:
            cmds += [
                commands.mkdir(groovy_cls),
                commands.mkdir(os.path.dirname(groovy_bf)),
                commands.prepare_build_file(groovys, srcdirs, groovy_bf, splitter='\\n'),
            ]
            cmds += commands.groovyc(
                groovy_bf,
                jdk_resource,
                groovy_compiler_resource,
                deps=pierced_cp,
                out_dir=groovy_cls,
                cwd=consts.BUILD_ROOT,
                custom_flags=None,
            )
        cmds += (
            commands.javac(
                bf,
                jdk_resource,
                deps=compile_cp + ([kotlin_cls] if with_kotlin else []) + ([groovy_cls] if with_groovy else []),
                out_dir=cls,
                encoding='UTF-8',
                custom_flags=flags,
                X='pkginfo:always',
                verbose=getattr(ctx.opts, 'be_verbose', False),
                nowarn=consts.NO_COMPILER_WARNINGS in target.plain,
                cwd=consts.BUILD_ROOT,
                use_error_prone=error_prone_resource,
                error_prone_flags=getattr(ctx.opts, 'error_prone_flags', None),
            )
            + [
                commands.copy_files(f, s, os.path.join(cls, p) if p else cls)
                for f, s, p in zip(resources, srcdirs, pps)
            ]
            + copy_dll_cmds
        )
        if with_kotlin:
            cmds += [commands.copy_all_files(kotlin_cls, cls)]
        if with_groovy:
            cmds += [commands.copy_all_files(groovy_cls, cls)]
        cmds += (
            generate_vcs_info_cmds
            + [commands.jar(os.curdir, cls_jar, jdk_resource, manifest=manifest, cwd=cls)]
            + commands.gen_jar_filter_cmds(target, cls_jar, consts.BUILD_ROOT)
        )

        tared_outs = []
        ins = inputs + node.files(pierced_cp + dlls)
        out_dir = os.path.join(graph_base.hacked_path_join(consts.BUILD_ROOT), path)
        if getattr(ctx.opts, "java_coverage", False):
            # Dump used sources 'path in package -> src path'
            classpath_source_file = graph_base.hacked_path_join(out_dir, os.path.splitext(cls_jar)[0] + ".cpsf")
            cmds += [commands.dump_classpath_source_files(classpath_source_file, source_files)]
            outs = node.files([cls_jar, classpath_source_file])
        else:
            outs = node.files([cls_jar])

        if ctx.opts.java_yndexing:
            need_resources.append('KYTHE')
            kindex_tar_path = graph_base.hacked_path_join(out_dir, 'kindex.tar')
            cmds += commands.javac(
                bf,
                jdk_resource,
                deps=pierced_cp,
                out_dir=out_dir,
                out_jar_name=cls_jar,
                encoding='UTF-8',
                custom_flags=flags,
                X='pkginfo:always',
                verbose=getattr(ctx.opts, 'be_verbose', False),
                nowarn=consts.NO_COMPILER_WARNINGS in target.plain,
                cwd=consts.BUILD_ROOT,
                kythe_tool=True,
            ) + [commands.tar('kindex.tar', '.kzip', cwd=out_dir)]
            outs += node.files([kindex_tar_path, os.path.join(out_dir, 'bfg.txt')])
            tared_outs += node.files([kindex_tar_path])

        if is_java_program:
            pierced_wars = [i for i in wars if i != this_war]
            ins += node.files(pierced_wars)
            cmds += collect_cmds(dlls + ([] if is_uberjar else cp) + wars, tar_path)
            outs += node.files([tar_path])
            tared_outs += node.files([tar_path])

    else:
        cmds = []
        ins = inputs
        outs = []
        tared_outs = []

    if is_uberjar:
        cp = ctx.classpath(path, consts.CLS)
        uberjar_resource = base.resolve_uberjar(ctx.global_resources, ctx.opts)
        prefix = target.plain.get(consts.UBERJAR_PREFIX, [[None]])[0][0]
        if prefix and not prefix.endswith('/'):
            prefix += '/'
        hide_exclude = [i.replace('/', '.') for i in sum(target.plain.get(consts.UBERJAR_HIDE_EXCLUDE, []), [])]
        path_exclude = sum(target.plain.get(consts.UBERJAR_PATH_EXCLUDE, []), [])
        if not outs:
            outs = node.files([cls_jar])
            # empty java_program, restore inputs/outputs
            dlls = ctx.dlls(path)
            pierced_cp = [x for x in cp if x != this_jar]
            cp = pierced_cp
            ins += node.files(pierced_cp + dlls)
        uberjar_manifest_main = target.plain.get(consts.UBERJAR_MANIFEST_TRANSFORMER_MAIN, [[None]])[0][0]
        uberjar_manifest_attrs = []
        for part in target.plain.get(consts.UBERJAR_MANIFEST_TRANSFORMER_ATTRIBUTE, []):
            assert ':' in part
            uberjar_manifest_attrs.append(
                ':'.join(['\\ '.join(part[: part.index(':')]), ' '.join(part[part.index(':') + 1 :])])
            )
        uberjar_append_resources = sum(target.plain.get(consts.UBERJAR_APPENDING_TRANSFORMER, []), [])
        uberjar_services_transformer = consts.UBERJAR_SERVICES_RESOURCE_TRANSFORMER in target.plain

        repack_root_dir = os.path.join(graph_base.hacked_path_join(consts.BUILD_ROOT), path)
        cmds += commands.make_uberjar_cmds(
            inputs=cp,
            out=this_jar,
            jdk_resource=jdk_resource,
            uberjar_resource=uberjar_resource,
            shade_prefix=prefix,
            shade_exclude=hide_exclude,
            path_exclude=path_exclude,
            manifest_main=uberjar_manifest_main,
            manifest_attributes=uberjar_manifest_attrs,
            append_transformers=uberjar_append_resources,
            service_transformer=uberjar_services_transformer,
            cwd=consts.BUILD_ROOT,
        ) + commands.repack_manifest(target, this_jar, jdk_resource, manifest=manifest, cwd=repack_root_dir)

    if consts.ADD_WAR in target.plain:
        cp = ctx.classpath(path, consts.CLS)
        dlls = ctx.dlls(path)
        pierced_cp = [x for x in cp if x != this_jar]

        cur_war_dir = os.path.join(consts.BUILD_ROOT, path, 'cur_war')
        res_war_dir = os.path.join(consts.BUILD_ROOT, path, 'res_war')
        war = target.output_war_path()

        dep_wars = [p for p in ctx.wars(path) if p != war]
        ins += node.files(dep_wars)

        dep_wars_unpacked = []
        for w in dep_wars:
            dest = w + '.unpacked'
            cmds += [commands.mkdir(dest), commands.jarx(w, jdk_resource, False, dest)]
            dep_wars_unpacked.append(dest)

        webapp_dir = os.path.join(cur_war_dir, 'WEB-INF')
        lib_dir = os.path.join(webapp_dir, 'lib')
        cmds.append(commands.mkdir(lib_dir))

        cmds.extend([commands.mv(p, os.path.join(lib_dir, os.path.basename(p))) for p in pierced_cp + dlls])
        cmds.append(commands.mv(cls, os.path.join(webapp_dir, 'classes')))
        # cmds.append(commands.move_if_exists(os.path.join(webapp_dir, 'classes', 'META-INF'), os.path.join(cur_war_dir, 'META-INF')))

        for sdir, pp, srcs_list, rsrcs_list in zip(war_srcdirs, war_pps, war_sources, war_resources):
            cmds.append(commands.copy_files(srcs_list, sdir, os.path.join(cur_war_dir, pp) if pp else cur_war_dir))
            cmds.append(commands.copy_files(rsrcs_list, sdir, os.path.join(cur_war_dir, pp) if pp else cur_war_dir))

        ins += war_inputs

        words = sum(target.plain.get(consts.ADD_WAR, []), [])
        kv = parse.extract_chunks(
            words, {consts.W_INCLUDE, consts.W_EXCLUDE, consts.W_INCLUDE_DEFAULT, consts.W_EXCLUDE_DEFAULT}
        )

        include_default = sum(kv.get(consts.W_INCLUDE_DEFAULT, []), [])
        exclude_default = sum(kv.get(consts.W_EXCLUDE_DEFAULT, []), [])

        includes = collections.defaultdict(list)
        excludes = collections.defaultdict(list)

        for ws in kv.get(consts.W_INCLUDE, []):
            p, patterns = parse_patterns(ws, consts.W_INCLUDE)
            includes[p].extend(patterns)

        for ws in kv.get(consts.W_EXCLUDE, []):
            p, patterns = parse_patterns(ws, consts.W_EXCLUDE)
            excludes[p].extend(patterns)

        cmds.append(commands.mkdir(res_war_dir))

        for war_unpacked in [cur_war_dir] + dep_wars_unpacked:
            dirname = os.path.relpath(os.path.dirname(war_unpacked), consts.BUILD_ROOT)

            if dirname in includes:
                incs = includes[dirname]

            else:
                incs = include_default

            if dirname in excludes:
                excs = excludes[dirname]

            else:
                excs = exclude_default

            if war_unpacked != cur_war_dir:
                cmds.append(commands.rm(os.path.join(war_unpacked, 'META-INF')))

            cmds.append(commands.move_what_matches(war_unpacked, res_war_dir, incs, excs))

        cmds.append(commands.gen_vcs_info_cmds(target, os.curdir, manifest, cwd=res_war_dir))
        cmds.append(commands.jar(os.curdir, war, jdk_resource, manifest=manifest, cwd=res_war_dir))
        outs.extend(node.files([war]))

    have_javac_generated_srcs = False
    if (
        javac_generated_srcs_dir
        and javac_generated_srcs_dir[0]
        and javac_generated_srcs_tar
        and javac_generated_srcs_tar[0]
        and ctx.opts.dump_sources
    ):
        have_javac_generated_srcs = True
        javac_generated_srcs_tar = graph_base.hacked_normpath(javac_generated_srcs_tar[0][0])
        javac_generated_srcs_dir = graph_base.hacked_normpath(javac_generated_srcs_dir[0][0])
        cmds += [
            commands.mkdir(javac_generated_srcs_dir),
            commands.tar_all(javac_generated_srcs_tar, javac_generated_srcs_dir, javac_generated_srcs_dir),
        ]
        outs.extend(node.files([javac_generated_srcs_tar]))

    if consts.EXTERNAL_DEPENDENCIES in target.plain:
        ins += node.files(
            node.try_resolve_inp_file(ctx.arc_root, path, dep)
            for dep in sum(target.plain.get(consts.EXTERNAL_DEPENDENCIES, []), [])
        )

    if consts.JAVA_SRCS in target.plain or consts.ADD_WAR in target.plain or is_uberjar:
        if have_javac_generated_srcs:
            kv = {consts.IDEA_NODE: True}
        else:
            kv = None
        yield node.JNode(
            path,
            cmds,
            ins,
            outs,
            tared_outs=tared_outs,
            res=path in ctx.rclosure,
            resources=need_resources,
            fake_id=target.fake_id(),
            kv=kv,
        )

    if add_jdk_resource and path in ctx.rclosure:
        for n in get_jdk(
            ctx.global_resources.get(consts.RESOURCE_WITH_JDK, '$({})'.format(consts.RESOURCE_WITH_JDK)),
            path + '/jdk.tar',
        ):
            yield n


def get_jdk(jdk_origin, path, untar=True):
    moved_jdk_tar = graph_base.hacked_path_join(consts.BUILD_ROOT, path)
    yield node.JNode(
        path,
        [commands.tar_all(moved_jdk_tar, jdk_origin)],
        ins=[],
        outs=node.files([moved_jdk_tar]),
        tared_outs=(node.files([moved_jdk_tar]) if untar else None),
        res=True,
    )


def empty_jar(path, target, ctx, type_):
    jdk_resource = base.resolve_jdk(
        ctx.global_resources, prefix=target.plain.get(consts.JDK_RESOURCE_PREFIX, '_NO_JDK_SELECTOR_'), opts=ctx.opts
    )

    out = target.output_jar_of_type_path(type_)
    cp = ctx.classpath(path, type_)
    wars = ctx.wars(path)
    dlls = ctx.dlls(path)
    ins = node.files([x for x in (cp + wars) if x != out] + dlls)
    res = path in ctx.rclosure
    tar_path = out[:-4]
    if type_ == consts.SRC:
        tar_path += '-sources'
        res &= ctx.opts.dump_sources
    tar_path += '.tar'

    manifest = target.output_vcs_mf_path() if 'EMBED_VCS' in target.plain else None

    if consts.ADD_DLLS_FROM_DEPENDS in target.plain:
        cmds = empty_jar_cmds(ctx.dlls(path), out, jdk_resource, target, manifest)
    else:
        cmds = empty_jar_cmds([], out, jdk_resource, target, manifest)

    outs = node.files([out])
    tared_outs = []

    if consts.JAVA_PROGRAM in target.plain:
        cmds += collect_cmds(cp + dlls + wars, tar_path)
        outs += node.files([tar_path])
        tared_outs += node.files([tar_path])

    return node.JNode(path, cmds, ins, outs, tared_outs=tared_outs, res=res, fake_id=target.fake_id())


def dlls_copy_cmds(dlls, destination):
    for dll in dlls:
        yield commands.cp(dll, os.path.join(destination, os.path.basename(dll)))
