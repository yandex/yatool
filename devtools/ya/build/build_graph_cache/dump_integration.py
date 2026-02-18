from __future__ import annotations

import copy
import os

import devtools.ya.build.genconf as genconf
from devtools.ya.build.build_graph_cache import layout as bg_cache_layout
from devtools.ya.build.build_graph_cache import target_tc as bg_cache_target_tc


def compute_dump_ymake_build_root_and_conf_dir(params) -> tuple[str | None, str | None]:
    """
    Compute ymake build-root (cache dir) and conf dir for `ya dump`-style ymake invocations.

    Goal: reuse *exactly the same* build-graph-cache directory layout as `ya make`.

    Returns:
      (build_root, custom_conf_dir) or (None, None) if build-graph-cache isn't available/enabled.
    """

    host = bg_cache_target_tc.normalize_host_platform(getattr(params, "host_platform", None))

    target_platforms = getattr(params, "target_platforms", None) or [{"platform_name": host}]
    target_platform = copy.deepcopy(target_platforms[0])
    target_tc = bg_cache_target_tc.resolve_target_tc(host, target_platform, params)

    base_dir = bg_cache_target_tc.compute_cache_base_dir(params, target_tc)
    if not base_dir:
        return None, None

    if bg_cache_layout.is_pic_only(getattr(params, "flags", {}), target_tc):
        build_root = os.path.join(base_dir, "pic")
        os.makedirs(build_root, exist_ok=True)
    else:
        build_root = base_dir

    conf_build_root = getattr(params, "custom_build_directory", None) or getattr(params, "bld_root", None)
    custom_conf_dir = genconf.detect_conf_root(getattr(params, "arc_root", None), conf_build_root)

    return build_root, custom_conf_dir
