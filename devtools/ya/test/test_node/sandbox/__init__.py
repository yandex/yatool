# coding: utf-8
import devtools.ya.build.gen_plan as gen_plan

import devtools.ya.test.dependency.sandbox_resource as sandbox_resource
import devtools.ya.test.dependency.sandbox_storage as sandbox_storage

import app_config
import devtools.ya.test.system.env as sysenv
import devtools.ya.test.util.shared as util_shared
import devtools.ya.test.util.tools as util_tools


def inject_download_sandbox_resource_node(graph, resource, opts, global_resources):
    resource_id = sandbox_resource.get_id(resource)
    fake_id = 9 + int(opts.dir_outputs_in_nodes)
    uid = "sandbox-resource-{}-{}-ro".format(resource_id, fake_id)
    custom_fetcher = getattr(opts, "custom_fetcher", None)
    oauth_token = getattr(opts, "oauth_token", None)

    if not graph.get_node_by_uid(uid):
        storage = sandbox_storage.get_sandbox_storage("$(BUILD_ROOT)", custom_fetcher, oauth_token)
        dir_output_paths = []
        fetcher_output_paths = storage.get_sandbox_fetcher_output_paths(resource_id)
        log_path = storage.get_resource_download_log_path('$(BUILD_ROOT)', resource_id)

        fetch_cmd = util_tools.get_test_tool_cmd(opts, "download", global_resources) + [
            "--storage-root",
            "$(BUILD_ROOT)",
            "--id",
            str(resource_id),
            "--log-path",
            log_path,
            "--rename-to",
            sandbox_storage.RESOURCE_CONTENT_FILE_NAME,
            "--resource-file",
            "$(RESOURCE_ROOT)/sbr/{}/resource".format(resource_id),
        ]

        if not app_config.have_sandbox_fetcher:
            node_requirements = gen_plan.get_requirements(opts)
        else:
            node_requirements = gen_plan.get_requirements(
                opts,
                {"network": "full"},
            )

        if opts.dir_outputs_in_nodes:
            resource_dir_output = storage.get_dir_outputs_resource_dir_path(resource_id)
            resource_tar = storage.get_dir_outputs_resource_tar_path(resource_id)
            dir_output_paths = [resource_dir_output]
            output_paths = [resource_tar]
            fetch_cmd += ["--dir-output-tared-path", resource_tar]
            output_paths.extend(fetcher_output_paths)
        else:
            output_paths = fetcher_output_paths

        output_paths.append(log_path)

        if opts.use_distbuild:
            fetch_cmd += ["--for-dist-build"]
            fetch_cmd += ["--log-level", "DEBUG"]
            timeout = 900
            fetch_cmd += ["--timeout", str(timeout)]
        else:
            timeout = 0
        if custom_fetcher and not opts.use_distbuild:
            fetch_cmd += ["--custom-fetcher", custom_fetcher]

        fetch_cmd += util_shared.get_oauth_token_options(opts)

        node = {
            "node-type": "download",
            "broadcast": False,
            "inputs": [],
            "uid": uid,
            "cwd": "$(BUILD_ROOT)",
            "priority": 0,
            "deps": [],
            "env": sysenv.get_common_env().dump(),
            "target_properties": {},
            "outputs": output_paths,
            "dir_outputs": dir_output_paths,
            'kv': {
                "p": "DL",
                "pc": 'light-cyan',
                "show_out": True,
            },
            "requirements": node_requirements,
            "resources": [
                {"uri": "sbr:" + str(resource_id)},
            ],
            "cmds": [
                {
                    "cmd_args": fetch_cmd,
                    "cwd": "$(BUILD_ROOT)",
                },
            ],
        }
        graph.append_node(node, add_to_result=False)

        if timeout:
            node["timeout"] = timeout

    return uid
