import os

import build.gen_plan2
import core.yarg


def gen_graph(params):
    import devtools.ya.app

    opts = core.yarg.merge_opts(build.build_opts.ya_make_options(free_build_targets=True))
    ya_make_extra = getattr(params, "ya_make_extra", [])
    build_params = opts.initialize(ya_make_extra)
    build_params.flags["TRAVERSE_RECURSE_FOR_TESTS"] = "yes"
    return build.gen_plan2.ya_make_graph(params, devtools.ya.app, extra_ya_make_opts=build_params)


def get_modules(graph, rel_targets=None, is_final=False, kv_p=None, target_properties=None):
    def check_module_params(module_target_properties, module_kv_p):
        if rel_targets and not any(module_target_properties.get("module_dir", "").startswith(x) for x in rel_targets):
            return False
        if kv_p and module_kv_p != kv_p:
            return False
        if target_properties and not all(module_target_properties.get(k) == v for k, v in target_properties.items()):
            return False
        return True

    modules = {}
    results = frozenset(graph.get("result"))
    for node in graph.get("graph"):
        if is_final and node["uid"] not in results:
            continue
        main_output = node["outputs"][0].replace("$(BUILD_ROOT)/", "")
        module_path = os.path.dirname(main_output)
        module_name = main_output
        module_target_properties = node.get("target_properties", {})
        module_kv = node.get("kv", {})
        if check_module_params(module_target_properties, module_kv.get("p")):
            if rel_targets and len(rel_targets) == 1:
                target = rel_targets[0]
                if module_name.startswith(target):
                    module_name = module_name[len(target) + 1 :] or module_name
            modules[module_name] = {
                "path": main_output,
                "module_path": module_path,
                "kv": module_kv,
                "uid": node["uid"],
            }
            modules[module_name].update(module_target_properties)
    return modules
