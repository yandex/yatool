import logging

from . import cpp
from . import extensions
from . import go
from . import java
from . import python
from . import ts
from . import rigel
from . import upload

logger = logging.getLogger(__name__)


def inject_coverage_nodes(arc_root, graph, suites, opts, platform_descriptor):
    coverage_uids = []
    resolvers_map = None
    if getattr(opts, "merge_coverage", False) or getattr(opts, 'upload_coverage_report', False):
        resolvers_map = {}
    inplace_cov = []
    for opt_name, inject_func in [
        ('java_coverage', java.inject_java_coverage_nodes),
        ('python_coverage', python.inject_python_coverage_nodes),
        ('sancov_coverage', cpp.inject_sancov_coverage_nodes),
        ('ts_coverage', ts.inject_ts_coverage_nodes),
        ('clang_coverage', cpp.inject_clang_coverage_nodes),
        ('go_coverage', go.inject_go_coverage_nodes),
        ('nlg_coverage', extensions.inject_nlg_coverage_nodes),
    ]:
        if getattr(opts, opt_name, False):
            coverage_uids += inject_func(graph, suites, resolvers_map, opts, platform_descriptor)
    if getattr(opts, "merge_coverage", False):
        if len(suites) > 200:
            logger.warning("%s tests in total, coverage merger may be work slowly", str(len(suites)))
        inplace_cov = rigel.inject_inplace_coverage_merger_node(graph, resolvers_map.keys(), suites, opts)
    if resolvers_map and getattr(opts, 'upload_coverage_report', False):
        assert suites
        # All suites has the same global_resources - take first
        global_resources = suites[0].global_resources

        # create root node only once (otherwise may hit the queue limit for yt account)
        create_table_node_uid = (
            upload.create_yt_root_maker_node(
                arc_root, graph, upload.get_coverage_table_chunks_count(), global_resources, opts
            )
            if opts.coverage_direct_upload_yt
            else None
        )
        # add coverage upload nodes to the results_node's deps
        # to avoid case when results_node fails and ya runner terminates graph execution
        coverage_uids += upload.inject_coverage_upload_nodes(
            arc_root, graph, resolvers_map, create_table_node_uid, opts
        )

    return coverage_uids + inplace_cov
