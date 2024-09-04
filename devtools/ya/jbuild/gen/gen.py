import os
import itertools
import six
import logging
import fnmatch

from . import node
from . import base
import jbuild.gen.actions.externals as externals
import jbuild.gen.actions.compile as compile
import jbuild.gen.actions.generate_scripts as generate_scripts
from .actions import fetch_test_data as fetch_test_data
from . import makelist_parser2 as mp
from . import configure
from . import consts
from core import stage_tracer
from yalibrary.vcs import vcsversion
from yalibrary import platform_matcher
from exts.strtobool import strtobool
import yalibrary.graph.base as graph_base
import yalibrary.graph.node as graph_node
from six.moves import map

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("jbuild")


def with_tests(opts):
    return opts.run_tests or opts.list_tests or opts.canonize_tests


def gen_ctx(
    arc_root, paths, opts, contrib_roots, cpp_graph=None, dart=None, target_tc=None, extern_global_resources=None
):
    paths = list(map(graph_base.hacked_normpath, paths))  # TODO: Maybe tuple?

    assert cpp_graph is not None
    assert dart is not None

    global_resources = extern_global_resources or {}
    for info in dart:
        if '_GLOBAL_RESOURCES_' in info:
            global_resources.update(graph_base.parse_resources(info['_GLOBAL_RESOURCES_']))

    import jbuild.commands

    jbuild.commands.BuildTools.YMAKE_BIN = getattr(opts, 'ymake_bin', None)

    if opts.export_to_maven and not opts.version:
        try:
            version = vcsversion.repo_config(opts.arc_root)[0]
            assert version != -1
            assert int(version)
            version = str(version)

        except Exception as e:
            raise mp.ParseError('Can\'t determine version for maven-export: {}'.format(str(e)))

    else:
        version = opts.version

    rc = paths[:]
    by_path = mp.obtain_targets_graph2(dart, cpp_graph)

    if getattr(opts, 'sonar', False) and by_path:
        assert graph_base.hacked_normpath(consts.SONAR_PATH) in by_path  # It's now always in by_path

    assert all(p in by_path for p in paths)

    rsrcs = base.resolve_possible_srcdirs(arc_root, by_path.values())

    if getattr(opts, 'sonar', False):
        sonar_paths = set()

        for p in rc:
            assert p in by_path

            if not by_path[p].is_dart_target():
                continue

            plain = by_path[p].plain

            if (
                mp.is_java(plain)
                and not mp.is_jtest(plain)
                and not mp.is_jtest_for(plain)
                and consts.JAVA_SRCS in plain
            ):
                if opts.sonar_project_filters:
                    for f in opts.sonar_project_filters:
                        if fnmatch.fnmatch(p, f):
                            sonar_paths.add(p)
                            break
                elif opts.sonar_default_project_filter:
                    if not p.startswith('devtools'):
                        sonar_paths.add(p)
                else:
                    sonar_paths.add(p)

        logger.debug('Run sonar analysis for %s module(s): %s', str(len(sonar_paths)), ', '.join(sorted(sonar_paths)))

    else:
        sonar_paths = set()

    target_platform = None
    if target_tc and 'platform_name' in target_tc:
        try:
            target_platform = platform_matcher.canonize_platform(
                platform_matcher.parse_platform(target_tc['platform_name'])['os']
            )
        except platform_matcher.PlatformNotSupportedException:
            target_platform = ''

    return base.Context(
        opts,
        arc_root,
        contrib_roots,
        set(paths),
        set(rc),
        by_path,
        rsrcs,
        version,
        sonar_paths,
        target_platform,
        global_resources,
    )


def iter_path_nodes(path, ctx):
    assert path in ctx.by_path

    def fix_io(n):
        node.resolve_ins(ctx.arc_root, n)
        node.resolve_outs(n)

        return n

    t = ctx.by_path[path]
    if not t.is_dart_target():
        yield graph_node.YmakeGrapNodeWrap(path, t.node, t.graph)
        if t.plain is not None:
            for nod in fetch_test_data.fetch_test_data(path, t, ctx):
                yield fix_io(nod)
        return

    it = [
        externals.externals(path, t, ctx),
        compile.compile(path, t, ctx),
        generate_scripts.generate_scripts(path, t, ctx),
        fetch_test_data.fetch_test_data(path, t, ctx),
    ]

    if ctx.opts.export_to_maven and (consts.JAVA_LIBRARY in t.plain or consts.JAVA_PROGRAM in t.plain):
        import jbuild.gen.actions.export_to_maven as mvn_export

        it.append(mvn_export.export_to_maven(path, t, ctx))

    if ctx.opts.get_deps and path in ctx.paths:
        import jbuild.gen.actions.get_deps as get_deps

        it.append(get_deps.get_deps(path, t, ctx))

    if ctx.opts.java_yndexing:
        import jbuild.gen.actions.codenav_gen as codenav_gen

        it.append(codenav_gen.gen_codenav(path, ctx, ctx.opts.kythe_to_proto_tool))

    outs = set()

    for n in map(fix_io, itertools.chain(*it)):
        for o in n.outs:
            outs.add(o)

        yield n

    def iter_fake(type_):
        p = ctx.by_path[path].output_jar_of_type_path(type_)

        if (p, node.FILE) not in outs and mp.is_java(t.plain):
            yield fix_io(compile.empty_jar(path, t, ctx, type_))

    for n in iter_fake(consts.CLS):
        yield n

    for n in iter_fake(consts.SRC):
        yield n


def iter_nodes(ctx):
    if ctx.opts.export_to_maven:
        for p in ctx.by_path:
            try:
                fill_test_map(p, ctx)
            except mp.ParseError as e:
                raise mp.ParseError('{}: {}'.format(p, str(e)))
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
    if ctx.opts.export_to_maven and ctx.maven_export_modules_list:
        import jbuild.gen.actions.export_to_maven as mvn_export

        yield mvn_export.export_root(ctx)


def fill_test_map(path, ctx):
    assert path in ctx.by_path
    target = ctx.by_path[path]

    if not target.is_dart_target() or (
        consts.JAVA_TEST not in target.plain
        and consts.JAVA_TEST_FOR not in target.plain
        and consts.TESTNG not in target.plain
        and consts.JUNIT5 not in target.plain
    ):
        return
    parent = None
    if consts.JAVA_TEST_FOR in target.plain:
        try:
            parent = target.plain[consts.JAVA_TEST_FOR][0][0]
        except IndexError:  # Ymake had to warn, ignore
            return

        if parent not in ctx.by_path:  # JTEST_FOR for missing module - ymake warns it, ignore
            return

        if not ctx.by_path[parent].is_dart_target():
            return
    else:
        parent_path = '/'.join(graph_base.hacked_normpath(path).split('/')[:-1])
        while not parent and parent_path:
            if parent_path in ctx.by_path and ctx.by_path[parent_path].is_dart_target():
                for key in (consts.JAVA_PROGRAM, consts.JAVA_LIBRARY):
                    if key in ctx.by_path[parent_path].plain:
                        parent = parent_path
            parent_path = '/'.join(parent_path.split('/')[:-1])

        if not parent:
            return

    ctx.maven_test_map[parent].add(path)


def iter_result(ctx, nodes):
    seen = set()

    if ctx.opts.get_deps:
        import jbuild.gen.actions.get_deps as get_deps

        for n in nodes:
            if n.tag == get_deps.GET_DEPS_TAG and n not in seen:
                seen.add(n)

                yield n

    if ctx.opts.idea_project_root:
        import jbuild.gen.actions.idea as idea

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

    if ctx.sonar_paths:
        for n in nodes:
            if n.path == consts.SONAR_PATH:
                yield n


def iter_scarab(nodes):
    for n in nodes:
        if n.path == 'tools/acceleo':
            yield n


def default_opts():
    import jbuild.jbuild_opts as jbuild_opts
    import core.yarg

    return core.yarg.merge_opts(jbuild_opts.jbuild_opts()).params()


def conf_warning(conf_errs):
    s = []

    for path, err in six.iteritems(conf_errs):
        s.extend(path + ': ' + x for x in str(err).split('\n'))

    return '\n'.join(s)


def gen(
    arc_root,
    paths,
    opts,
    contrib_roots,
    cpp_graph=None,
    ev_listener=None,
    dart=None,
    target_tc=None,
    extern_global_resources=None,
):
    with stager.scope('insert_java-gen_ctx'):
        ctx = gen_ctx(
            arc_root,
            paths,
            opts,
            contrib_roots,
            cpp_graph=cpp_graph,
            dart=dart,
            target_tc=target_tc,
            extern_global_resources=extern_global_resources,
        )

    insert_java_detect_unversioned = stager.start('insert_java-detect_unversioned')
    nodes, ins_unresolved = graph_node.merge(list(iter_nodes(ctx)))

    node.resolve_io_dirs(nodes, ctx)

    res = list(iter_result(ctx, nodes))

    achievable = set()
    graph_base.traverse(res + list(iter_scarab(nodes)), after=achievable.add)

    ins_unresolved_ = graph_node.calc_uids(arc_root, achievable)

    for n, ins in six.iteritems(ins_unresolved_):
        ins_unresolved[n].extend(ins)

    for n, ins in six.iteritems(ins_unresolved):
        if n in achievable:
            ctx.errs[n.path].missing_inputs.extend([x[0] for x in ins])
            ctx.errs[n.path].missing_inputs.extend(
                [
                    os.path.join(n.path, x[0])
                    for x in ins
                    if not graph_base.in_source(x[0]) and not graph_base.in_build(x[0])
                ]
            )  # For selective checkout from JAVA_SRCS

    for e in six.itervalues(ctx.errs):
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
        # s = conf_warning(ctx.errs)
        if not opts.continue_on_fail:
            raise configure.ConfigureError('Configure error (use -k to proceed)')

        # if opts.continue_on_fail:
        #     logger.error(s)
        #
        # else:
        #     raise configure.ConfigureError(s)

    ctx.nodes = nodes
    return task, ctx, ctx.errs


def gen_build_graph(
    arc_root,
    paths,
    dart,
    make_opts,
    contrib_roots,
    cpp_graph=None,
    ev_listener=None,
    target_tc=None,
    extern_global_resources=None,
):
    opts = default_opts()
    opts.__dict__.update(make_opts.__dict__)  # XXX
    task, ctx, errs = gen(
        arc_root,
        paths,
        opts,
        contrib_roots,
        cpp_graph=cpp_graph,
        ev_listener=ev_listener,
        dart=dart,
        target_tc=target_tc,
        extern_global_resources=extern_global_resources,
    )

    return task, errs, ctx
