from __future__ import absolute_import

import shutil
import sys
import os
import logging
import json

import exts.tmp as tmp
import jbuild.gen.consts as consts
import jbuild.gen.actions.idea as idea
import build.graph as bg
import build.build_opts as bo
import build.ya_make as ya_make
import core.event_handling
import core.yarg

logger = logging.getLogger(__name__)


def filter_out_results(graph, ctx):
    initial_results = set(graph['result'])
    start_nodes = {}
    internal_nodes = set()
    for node in graph['graph']:
        if node['uid'] not in initial_results:
            continue
        module_tag = node.get('target_properties', dict()).get('module_tag', '')
        if module_tag == 'jar_testable':
            internal_nodes.add(node['uid'])
        path = node.get('target_properties', dict()).get('module_dir', '')
        if path in ctx.by_path and ctx.by_path[path].is_idea_target():
            # This 2 conditions mut be in sync with `process_path` function in devtools/ya/jbuild/gen/actions/idea.py
            if (
                consts.JAVA_SRCS not in ctx.by_path[path].plain
                and consts.ADD_DLLS_FROM_DEPENDS not in ctx.by_path[path].plain
            ):
                continue
            if not ctx.opts.local or path in ctx.rclosure:
                start_nodes[node['uid']] = node

    results = set()
    for res in initial_results:
        if res in start_nodes:
            results.update(dep for dep in start_nodes[res].get('deps', []) if dep not in start_nodes)
        elif res not in internal_nodes:
            results.add(res)

    return list(results)


def get_dependencies_modules(dump_result):
    deps = set()
    lines = [i.rstrip() for i in dump_result.split('\n') if i.strip()]
    current = {}

    def flush():
        if current.get('dir', None) and current.get('type', None) in ('JAVA_PROGRAM', 'JAVA_LIBRARY'):
            if current['dir'].startswith('$S/'):
                candidate = current['dir'][3:]
                if not candidate.startswith('contrib/java'):
                    deps.add(candidate)
            current['dir'] = None
            current['type'] = None

    for line in lines:
        if not line.startswith(' '):
            flush()
        line = line.strip()
        parts = line.split(':')
        if len(parts) != 2:
            continue
        if parts[0].strip() == 'Module Dir':
            current['dir'] = parts[1].strip()
        if parts[0].strip() == 'Var MODULE_TYPE':
            current['type'] = parts[1].strip()
    flush()
    return deps


def copy_shared_index_config(jopts):
    targets = jopts.abs_targets
    if len(targets) != 1:
        logger.warning("Unable to copy shared index config for more than one targets")
        return
    idea_project_root = jopts.idea_project_root
    shared_config_dst = os.path.join(idea_project_root, "intellij.yaml")

    target = targets[0]
    shared_index_config = os.path.join(target, "intellij.yaml")
    if os.path.isfile(shared_index_config) and not (shared_index_config == shared_config_dst):
        logger.info("Copy shared index config %s to project dir %s", shared_index_config, target)
        shutil.copy(shared_index_config, shared_config_dst)


def do_idea(params):
    if params.dry_run:
        logger.info("--- DRY-RUN MODE ---")

    ya_make_opts = core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
    jopts = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra), params)
    jopts.dump_sources = True
    jopts.create_symlinks = False
    jopts.flags['TRAVERSE_RECURSE_FOR_TESTS'] = 'yes'
    jopts.flags['YA_IDE_IDEA'] = 'yes'
    jopts.flags['SOURCES_JAR'] = 'yes'
    if jopts.generate_tests_for_deps:
        from handlers.dump import do_module_info, DumpModuleInfoOptions

        jopts.__dict__.update(DumpModuleInfoOptions().__dict__)
        res = do_module_info(jopts, False)
        deps = get_dependencies_modules(res.stdout)
        for dep in deps:
            if dep not in jopts.rel_targets:
                jopts.rel_targets.append(dep)
                jopts.abs_targets.append(os.path.join(jopts.arc_root, dep))

    import app_ctx  # XXX: via args

    subscribers = [
        ya_make.DisplayMessageSubscriber(jopts, app_ctx.display),
        core.event_handling.EventToLogSubscriber(),
    ]
    if getattr(app_ctx, 'evlog', None):
        subscribers.append(ya_make.YmakeEvlogSubscriber(app_ctx.evlog.get_writer('ymake')))

    jopts.flags['BUILD_LANGUAGES'] = 'JAVA'
    jopts.all_outputs_to_result = True
    jopts.add_result.append('.jar')
    jopts.add_result.append('.so')
    jopts.add_result.append('.dylib')
    jopts.add_result.append('.dll')
    jopts.add_result.append('.gentar')
    jopts.add_result.append('.java')
    jopts.add_result.append('.kt')
    jopts.debug_options.append('c')

    recurses = []
    with tmp.temp_dir() as dump_dir:
        jopts.dump_file_path = dump_dir
        with app_ctx.event_queue.subscription_scope(*subscribers):
            graph, _, _, ctx, _ = bg.build_graph_and_tests(jopts, check=True, display=app_ctx.display)
        target_dumps = [
            dump
            for dump in os.listdir(dump_dir)
            if not dump.startswith('tools-') and not dump.endswith('-global.txt') and not dump.endswith('-pic.txt')
        ]
        for dump in target_dumps:
            with open(os.path.join(dump_dir, dump), 'r') as dump_file:
                for recurse in json.load(dump_file):
                    recurses.append(recurse)

    ctx.rclosure.update(tgt for tgt in recurses if tgt in ctx.by_path)
    graph['result'] = filter_out_results(graph, ctx)

    with tmp.temp_dir() as od:
        jopts.output_root = od

        if not params.dry_run:
            builder = ya_make.YaMake(jopts, app_ctx, graph=graph, tests=[])
            builder.go()
            rc = builder.exit_code

            if rc != 0:
                sys.exit(rc)

        for f in idea.up_funcs(ctx, ctx.nodes, od, params.idea_project_root, params.dry_run):
            f()

    if not params.dry_run and jopts.copy_shared_index_config:
        copy_shared_index_config(jopts)
