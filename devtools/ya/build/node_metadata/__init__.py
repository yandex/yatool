"""Node requirements and platform tags without build configuration imports."""

import copy

from exts.strtobool import strtobool
import yalibrary.platform_matcher as pm


class PlatformNameError(Exception):
    pass


def get_requirements(opts, extra_reqs=None):
    if opts and getattr(opts, "default_node_requirements"):
        reqs = dict(opts.default_node_requirements)
    else:
        reqs = {}

    if extra_reqs:
        reqs.update(extra_reqs)
    return reqs


def real_build_type(tc, opts):
    return tc.get('build_type') or opts.build_type


PLATFORM_FLAGS = {'MUSL', 'ALLOCATOR', 'FAKEID', 'RACE'}
FLAGS_MAPPING = {'USE_LTO': 'lto', 'USE_THINLTO': 'thinlto', 'MUSL': 'musl', 'USE_AFL': 'AFL', 'RACE': 'race'}


def _fmt_tag(k, v):
    if k == 'SANITIZER_TYPE':
        return v[0] + 'san'

    try:
        yes = strtobool(v)
    except ValueError:
        yes = 0

    if k in FLAGS_MAPPING:
        return FLAGS_MAPPING[k] if yes else None

    return '{k}={v}'.format(k=k, v=v)


# See build_graph_and_tests::iter_target_flags
def prepare_tags(tc, extra_flags, opts, validate_platform_id=False):
    tags = []

    flags = copy.deepcopy(tc.get('flags', {}))

    flags.update({k: v for k, v in extra_flags.items() if k in PLATFORM_FLAGS})

    tags.append(platform := pm.stringize_platform(tc['platform']['target']).lower())
    tags.append(build_type := real_build_type(tc, opts))

    if platform_id := tc.get('platform_id'):
        if validate_platform_id and not platform_id.startswith(f'{platform}-{build_type}'):
            raise PlatformNameError(
                f'Platform id {platform_id} must start with base platform "{platform}-{build_type}" ({tc})'
            )

    if flags:
        tags.extend(sorted([_f for _f in [_fmt_tag(k, v) for k, v in flags.items()] if _f]))

    return tags, platform, platform_id
