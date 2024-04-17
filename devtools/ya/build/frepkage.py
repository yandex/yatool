import functools
import itertools
import logging

import six

import os
import sys
import tempfile

from build import graph as bgraph
import exts.yjdump as yjdump
import exts.archive as archive

logger = logging.getLogger(__name__)


def create_frepkage(build_context, graph, arc_root):
    import devtools.ya.build.source_package as source_package

    '''
        Build frozen repository package
    '''

    def dump_build_context(temp_dir):
        result_uids = graph['result']

        build_context.update(
            {
                'graph': graph,
                'lite_graph': bgraph.build_lite_graph(graph),
                # Strip irrelevant tests
                'tests': {uid: data for uid, data in six.iteritems(build_context['tests']) if uid in result_uids},
            }
        )

        ctx_file = os.path.join(temp_dir, 'build_context.json')

        with open(ctx_file, 'wb') as afile:
            yjdump.dump_context_as_json(build_context, afile)

        return ctx_file

    def mine_inputs_and_fix_cmds():
        external_inputs = {}
        nodes_required_fix = set()
        src_inputs_map = dict(graph['inputs'])
        get_count = functools.partial(next, itertools.count())

        for node in graph['graph']:
            cmd_fix_required = False
            # Mine external files to pack them into the package
            for cmd in node.get('cmds', []):
                for arg in cmd.get('cmd_args'):
                    if arg.startswith('/'):
                        if arg in external_inputs:
                            cmd_fix_required = bool(external_inputs[arg])
                        else:
                            if os.path.isfile(arg):
                                external_inputs[arg] = 'external_inputs/{}/{}'.format(
                                    get_count(), os.path.basename(arg)
                                )
                                cmd_fix_required = True
                            else:
                                external_inputs[arg] = None
            if cmd_fix_required:
                nodes_required_fix.add(node['uid'])

        node_inputs = {i: None for node in graph['graph'] for i in node['inputs']}
        src_inputs_map = bgraph.union_inputs(src_inputs_map, node_inputs)
        src_inputs = source_package.get_inputs(src_inputs_map, arc_root)

        def fix_cmd(cmd):
            return ['$(FREPKAGE_ROOT)/{}'.format(external_inputs[x]) if external_inputs.get(x) else x for x in cmd]

        def fix_node(node):
            # Don't change node data inplace - it would change node in the full graph as well
            node = dict(node)
            cmds = node['cmds'] = [dict(cmd) for cmd in node['cmds']]
            for cmd in cmds:
                cmd['cmd_args'] = fix_cmd(cmd['cmd_args'])
            return node

        # Not likely
        # External inputs might be tools provided from the outside using --ymake-bin PATH, --test-tool-bin PATH, etc
        if nodes_required_fix:
            graph['graph'] = [fix_node(n) if n['uid'] in nodes_required_fix else n for n in graph['graph']]

        return src_inputs, external_inputs

    temp_dir = tempfile.mkdtemp()
    tar_file = os.path.join(temp_dir, 'frepkage.tar.gz')

    inputs, external_inputs = mine_inputs_and_fix_cmds()
    # Dump graph when all modifications are done
    ctx_file = dump_build_context(temp_dir)

    paths_to_arch = []
    paths_to_arch.append(
        (
            ctx_file,
            os.path.basename(ctx_file),
        )
    )
    paths_to_arch.append(
        (
            os.path.realpath(sys.executable),
            'ya-bin',
        )
    )
    # Pack external inputs
    for filename, arcname in sorted(six.iteritems(external_inputs)):
        if arcname:
            logger.warn('Adding external input to frepkage: %s', filename)
            paths_to_arch.append(
                (
                    os.path.realpath(filename),
                    arcname,
                )
            )
        else:
            logger.debug('Skip external data: %s', filename)
    # Pack source inputs
    for path, hash_value in inputs:
        paths_to_arch.append(
            (
                os.path.join(arc_root, path),
                os.path.join('arcadia', path),
            )
        )
        source_package.check_hash(path, arc_root, hash_value)

    archive.create_tar(
        paths=paths_to_arch, tar_file_path=tar_file, compression_filter="gzip", compression_level=1, fixed_mtime=None
    )
    return tar_file
