from __future__ import annotations

import copy

import devtools.ya.build.genconf as genconf
from devtools.ya.build.build_graph_cache import layout as bg_cache_layout

try:
    import yalibrary.build_graph_cache as bg_cache
except ImportError:
    bg_cache = None


def normalize_host_platform(host_platform: str | None) -> str:
    if host_platform:
        return genconf.mine_platform_name(host_platform)
    return genconf.host_platform_name()


def normalize_target_platform_name(platform_name: str | None, host: str) -> str:
    platform_name = platform_name or host
    if platform_name == "host_platform":
        platform_name = genconf.host_platform_name()
    return genconf.mine_platform_name(platform_name)


def should_ignore_mismatched_xcode_version(opts, target_platform: dict) -> bool:
    target_flag = target_platform.get("flags", {}).get("IGNORE_MISMATCHED_XCODE_VERSION") == "yes"
    global_flag = getattr(opts, "flags", {}).get("IGNORE_MISMATCHED_XCODE_VERSION") == "yes"
    return bool(target_flag or global_flag)


def resolve_target_tc(host: str, target_platform: dict, opts) -> dict:
    """
    Build target toolchain config dict (target_tc) similarly to `ya make` (resolve_target()).

    This logic is shared between `ya make` and `ya dump` to keep build-graph-cache layout stable.
    """

    target = copy.deepcopy(target_platform)
    platform_name = normalize_target_platform_name(target.get("platform_name"), host)
    target["platform_name"] = platform_name

    c_compiler = target.get("c_compiler") or getattr(opts, "c_compiler", None)
    cxx_compiler = target.get("cxx_compiler") or getattr(opts, "cxx_compiler", None)

    target.update(
        genconf.gen_cross_tc(
            host,
            platform_name,
            c_compiler=c_compiler,
            cxx_compiler=cxx_compiler,
            ignore_mismatched_xcode_version=should_ignore_mismatched_xcode_version(opts, target),
        )
    )
    return target


def compute_cache_base_dir(opts, target_tc: dict, platform_name_override: str | None = None) -> str | None:
    """
    Compute base cache dir (without /pic) inside `--build-graph-cache-dir` using `ya make` layout.
    """

    if bg_cache is None:
        return None

    cache_root = bg_cache.configure_build_graph_cache_dir(opts)
    if not cache_root:
        return None

    return bg_cache_layout.build_graph_cache_dirname(target_tc, cache_root, platform_name_override)
