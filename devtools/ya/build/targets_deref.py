import os

import build.targets
import devtools.ya.core.respawn
import devtools.ya.core.config
import devtools.ya.core.yarg


# XXX: remove
def intercept(func, params, old_style_bld_root=False):
    kwargs = params.as_dict()

    source_root = kwargs.pop('custom_source_root', None)
    old_targets = kwargs.pop('build_targets', [])

    custom_build_directory = kwargs.pop('custom_build_directory', None)
    build_root = kwargs.pop('custom_build_root', None)

    info = build.targets.resolve(source_root, old_targets)
    devtools.ya.core.respawn.check_for_respawn(info.root)

    kwargs['abs_targets'] = info.targets
    kwargs['rel_targets'] = [os.path.relpath(x, info.root) for x in info.targets]
    kwargs['arc_root'] = info.root

    if custom_build_directory:
        bld_dir = os.path.abspath(custom_build_directory)
    else:
        if old_style_bld_root:
            # XXX: wipe out
            bld_dir = os.path.normpath(os.path.join(info.root, '..', 'ybuild'))
        else:
            bld_dir = devtools.ya.core.config.build_root()

    kwargs['bld_dir'] = bld_dir
    kwargs['bld_root'] = build_root or os.path.join(bld_dir, 'build_root')
    kwargs['custom_build_directory'] = kwargs.get('bld_root')  # XXX: deprecated

    if kwargs.get('output_root'):
        kwargs['output_root'] = os.path.abspath(kwargs.get('output_root'))

    return func(devtools.ya.core.yarg.Params(**kwargs))
