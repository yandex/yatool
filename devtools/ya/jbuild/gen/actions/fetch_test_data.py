import os

import devtools.ya.jbuild.commands as commands
import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.node as node

import yalibrary.graph.commands as graph_commands

import devtools.ya.test.util.tools as util_tools


def fetch_test_data(path, target, ctx):
    if getattr(ctx.opts, 'omit_test_data', None):
        return
    prefix = os.path.join('$(BUILD_ROOT)', path, 'sbr_test_data')
    for sbr_id in sum(target.plain.get(consts.TEST_DATA_SANDBOX, []), []):
        sbr_id = sbr_id.split('=')[0]
        data_dir = os.path.join(prefix, sbr_id)
        resource_name = 'resource'
        resource_info = 'resource_info.json'
        fetch_cmd = graph_commands.Cmd(
            cmd=util_tools.get_test_tool_cmd(ctx.opts, "download", ctx.global_resources)
            + [
                '--storage-root',
                '$(BUILD_ROOT)',
                '--id',
                sbr_id,
                "--resource-file",
                "$(RESOURCE_ROOT)/sbr/{}/resource".format(sbr_id),
                '--log-path',
                os.path.join('$(BUILD_ROOT)', 'res{}.log'.format(sbr_id)),
                '--rename-to',
                resource_name,
            ],
            cwd='$(BUILD_ROOT)',
            inputs=[],
            resources=[{"uri": "sbr:{}".format(sbr_id)}],
        )
        yield node.JNode(
            path,
            [
                fetch_cmd,
                commands.mkdir(data_dir),
                commands.move_if_exists(
                    os.path.join('$(BUILD_ROOT)', 'sandbox-storage', sbr_id, resource_name),
                    os.path.join(data_dir, resource_name),
                ),
                commands.move_if_exists(
                    os.path.join('$(BUILD_ROOT)', 'sandbox-storage', sbr_id, resource_info),
                    os.path.join(data_dir, resource_info),
                ),
            ],
            [],
            node.files([os.path.join(data_dir, resource_name), os.path.join(data_dir, resource_info)]),
            res=False,
            kv={consts.IDEA_NODE: True, consts.TEST_DATA_SANDBOX: data_dir},
        )
