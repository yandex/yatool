"""A full custom graph stored on disk, with an eagerly loaded local projection.

This is a serialization source, not a lazy mapping. Full node commands are never
materialized: they are streamed from the immutable original context on dump.
Compressed contexts are decompressed directly into each consumer, without a
temporary uncompressed file. The caller must keep the source unchanged and
available until serialization/retries have finished.
"""

import logging
import os
import struct

import exts.yjson as json

from devtools.ya.build.graph import LITE_GRAPH_NODE_KEYS
from devtools.ya.core import stage_tracer
from devtools.ya.yalibrary import sjson
from exts.decompress import udopen
from yalibrary.graph.source import GraphSource

logger = logging.getLogger(__name__)
stager = stage_tracer.get_tracer('disk_graph')


def _native_input_supported(path):
    """Keep legacy UC framing on the existing Python decoder path."""
    if not str(path).endswith('.uc'):
        return True
    with open(path, 'rb') as source:
        prefix = source.read(4)
        if len(prefix) != 4:
            return False
        header_size = struct.unpack('<I', prefix)[0]
        if not 0 < header_size <= 65536:
            return False
        try:
            header = json.loads(source.read(header_size))
        except (ValueError, UnicodeError):
            return False
        if not isinstance(header, dict) or not isinstance(header.get('codec'), str):
            return False
        while True:
            prefix = source.read(4)
            if not prefix or len(prefix) != 4:
                return True  # The native reader also diagnoses truncated input.
            block_size = struct.unpack('<I', prefix)[0]
            if not block_size:
                return False  # Python UC accepts a zero-length EOF marker.
            if block_size > 100000000:
                return True  # Both readers reject oversized compressed blocks.
            source.seek(block_size, os.SEEK_CUR)


class DiskGraph(GraphSource):
    def __init__(self, path, local_graph, has_inputs=True, native_input=None):
        super().__init__(local_graph)
        self.path = path
        self._has_inputs = has_inputs
        self._native_input = _native_input_supported(path) if native_input is None else native_input

    def __call__(self, output):
        replacements = {'conf': self.local_graph['conf'], 'result': self.local_graph['result']}
        if self._has_inputs:
            # TARED packaging expands the shared inputs map in place.
            replacements['inputs'] = self.local_graph['inputs']
        with stager.scope('stream_custom_graph'):
            if self._native_input:
                sjson.rewrite_file(self.path, output, root_key='graph', replacements=replacements)
            else:
                with udopen(self.path, 'rb') as source:
                    sjson.rewrite_object(source, output, root_key='graph', replacements=replacements)


def can_load_from_disk(params):
    # These modes inspect/transform full node descriptions before the lite graph
    # is built. Keep their existing behavior until they have streaming consumers.
    return params.use_distbuild and not (
        params.custom_json
        or params.save_context_to
        or params.dump_graph
        or params.show_command
        or params.list_tests
        or params.bazel_remote_warming
        or params.bazel_remote_warming_manifest
        or params.yt_replace_result
        or params.dist_cache_evict_cached
        or params.frepkage_target_uid
        or params.force_create_frepkage
        or params.stat_only_report_file
    )


def load_custom_context(params):
    if not can_load_from_disk(params):
        with udopen(params.custom_context, 'rb') as source:
            return sjson.load(source, intern_keys=True, intern_vals=True)

    path = os.path.abspath(params.custom_context)
    native_input = _native_input_supported(path)
    options = dict(
        intern_keys=True,
        intern_vals=True,
        array_item_path=('graph', 'graph'),
        array_item_key_white_list=LITE_GRAPH_NODE_KEYS,
        compact_paths=(('graph', 'graph'), ('graph', 'inputs')),
    )
    with stager.scope('load_custom_context_projection'):
        if native_input:
            data = sjson.load_file(path, **options)
        else:
            with udopen(path, 'rb') as source:
                data = sjson.load(source, **options)
    graph = data.get('graph')
    if not isinstance(graph, dict):
        # Preserve the existing error handling for contexts without a graph.
        return data
    local_graph = {
        'inputs': graph.get('inputs', {}),
        'result': graph['result'],
        'graph': graph['graph'],
        'conf': graph['conf'],
    }
    data['graph'] = DiskGraph(path, local_graph, has_inputs='inputs' in graph, native_input=native_input)
    logger.debug('Loaded %d custom graph nodes as a disk-backed projection', len(local_graph['graph']))
    return data
