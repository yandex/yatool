import logging
import time

from core import stage_tracer
import exts.fs
import exts.yjdump as yjdump
import exts.yjson as json
from exts.compress import ucopen
from exts.decompress import udopen
from yalibrary.monitoring import YaMonEvent

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer("build_handler")
stage_begin_time = {}


def _dump_results(builder, owners):
    import os

    def _dump_json(root, filename, content):
        with open(os.path.join(root, filename), 'w') as fp:
            json.dump(content, fp)

    build_result = builder.build_result

    exts.fs.ensure_dir(builder.misc_build_info_dir)
    _dump_json(builder.misc_build_info_dir, 'failed_dependants.json', build_result.failed_deps)
    _dump_json(builder.misc_build_info_dir, 'configure_errors.json', builder.ctx.configure_errors)
    _dump_json(builder.misc_build_info_dir, 'make_files.json', builder.get_make_files())
    _dump_json(builder.misc_build_info_dir, 'build_errors.json', build_result.build_errors)
    _dump_json(builder.misc_build_info_dir, 'ok_nodes.json', build_result.ok_nodes)
    _dump_json(builder.misc_build_info_dir, 'owners_list.json', owners)
    _dump_json(builder.misc_build_info_dir, 'targets.json', builder.targets)
    _dump_json(builder.misc_build_info_dir, 'results2.json', builder.make_report())


def monitoring_stage_started(stage_name):
    stager.start(stage_name)
    stage_begin_time[stage_name] = time.time()


def monitoring_stage_finished(stage_name):
    stager.finish(stage_name)
    if stage_name not in stage_begin_time:
        logger.error("stage_end without stage_begin for '%s'", stage_name)
    else:
        YaMonEvent.send('EYaStats::ContextTime', time.time() - stage_begin_time[stage_name])
        del stage_begin_time[stage_name]


def do_ya_make(params):
    from build import ya_make

    import app_ctx  # XXX

    monitoring_stage_started('ya_make_handler')
    context_generating_stage = stager.start('context_generating')

    if not params.custom_context:
        with stager.scope("build_graph_cache_configuration"):
            ya_make.configure_build_graph_cache_dir(app_ctx, params)

    if getattr(params, 'build_graph_cache_force_local_cl', False):
        with stager.scope("force_changelist_creation"):
            ya_make.prepare_local_change_list(app_ctx, params)

    # XXX
    if getattr(params, 'make_context_on_distbuild_only', False) or getattr(params, 'make_context_only', False):
        from devtools.ya.build.remote import remote_graph_generator

        remote_graph_generator.generate(params, app_ctx)
        context_generating_stage.finish()
        monitoring_stage_finished('ya_make_handler')
        return 0

    # XXX
    if getattr(params, 'make_context_on_distbuild', False):
        from devtools.ya.build.remote import remote_graph_generator

        context = remote_graph_generator.generate(params, app_ctx)
        builder = context.builder
    elif params.custom_context:
        with udopen(params.custom_context) as custom_context_file:
            custom_context_json = json.load(custom_context_file)
        if params.dist_priority:
            custom_context_json['graph']['conf']['priority'] = params.dist_priority
        if params.distbuild_cluster:
            custom_context_json['graph']['conf']['cluster'] = params.distbuild_cluster
        elif params.coordinators_filter:
            custom_context_json['graph']['conf']['coordinator'] = params.coordinators_filter
        if params.distbuild_pool:
            custom_context_json['graph']['conf']['pool'] = params.distbuild_pool
        if params.cache_namespace:
            for graph_section in ('graph', 'lite_graph'):
                if graph_section in custom_context_json:
                    custom_context_json[graph_section]['conf']['namespace'] = params.cache_namespace
        context = ya_make.BuildContext.load(params, app_ctx, custom_context_json)
        builder = context.builder
    else:
        builder = ya_make.YaMake(params, app_ctx)
        context = None

    context_generating_stage.finish()

    exit_code = 0
    if getattr(params, 'save_context_to', None) is None:
        with stager.scope('build'):
            exit_code = builder.go()
        if builder.misc_build_info_dir:
            with stager.scope('dump_results'):
                context = context or ya_make.BuildContext(builder)
                _dump_results(builder, context.owners)
    else:
        stager.start('save_context')
        with stager.scope('create_build_context'):
            context = context or ya_make.BuildContext(builder)

        context_json = context.save()
        graph_json = context_json.pop('graph')

        with ucopen(params.save_context_to, mode="wb") as context_file:
            yjdump.dump_context_as_json(context_json, context_file)

        with ucopen(params.save_graph_to, mode="wb") as graph_file:
            yjdump.dump_graph_as_json(graph_json, graph_file)

        stager.finish('save_context')

    monitoring_stage_finished('ya_make_handler')

    return 0 if params.ignore_nodes_exit_code else exit_code
