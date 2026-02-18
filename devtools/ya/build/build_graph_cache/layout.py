import copy
import logging
import os

import exts.fs
import exts.hashing as hashing
import exts.yjson as json
from exts.strtobool import strtobool

logger = logging.getLogger(__name__)


def is_pic_only(flags: dict, target_tc: dict) -> bool:
    """
    Honor -DPIC and --target-platform-flag=PIC as force_pic.

    This is intentionally kept consistent with `devtools.ya.build.graph._GraphMaker._pic_only`.
    """

    return strtobool(flags.get("PIC", "no")) or strtobool(target_tc.get("flags", {}).get("PIC", "no"))


def build_graph_cache_dirname(target_tc_orig: dict, cache_dir_root: str, platform_name: str | None = None) -> str:
    """
    Create (if needed) and return directory name used by ymake cache inside build-graph-cache root.

    The naming algorithm and side effects (creating directory and writing target.json) must remain
    compatible with what `ya make` historically used.
    """

    target_tc = copy.deepcopy(target_tc_orig)
    flags = target_tc.get("flags", {})
    for key in ("RECURSE_PARTITIONS_COUNT", "RECURSE_PARTITION_INDEX"):
        flags.pop(key, None)

    # These fields should not affect cache key
    target_tc.pop("targets", None)
    target_tc.pop("executable_path", None)
    target_tc.pop("tool_var", None)

    json_str = json.dumps(target_tc, sort_keys=True)

    base_name = ""
    if platform_name:
        base_name += platform_name
    elif "platform_name" in target_tc:
        base_name += target_tc["platform_name"]

    base_name += "-" + hashing.md5_value(json_str)
    cache_dir = os.path.join(cache_dir_root, base_name)

    logger.debug("Creating ymake directory for target_tc: %s", json.dumps(target_tc, sort_keys=True, indent=2))

    if not os.path.exists(cache_dir):
        exts.fs.ensure_dir(cache_dir)
        logger.debug("Created ymake directory")

    target_json = os.path.join(cache_dir, "target.json")
    if not os.path.exists(target_json):
        logger.debug("Wrote target.json into ymake directory")
        with open(target_json, "w") as f:
            json.dump(target_tc, f, sort_keys=True, indent=2)

    logger.debug("Done with ymake directory")
    return cache_dir
