import os
import build.targets
import core.yarg
import core.respawn
import logging
from exts.windows import win_path_fix

logger = logging.getLogger(__name__)


def resolve_and_respawn(params, handler_python_major_version):
    kwargs = params.as_dict()
    source_root = kwargs.pop('custom_source_root', None)
    old_targets = kwargs.pop('build_targets', [])

    custom_build_directory = kwargs.pop('custom_build_directory', None)
    build_root = kwargs.pop('custom_build_root', None)

    ya_bin3_required = kwargs.get('ya_bin3_required', None)

    if ya_bin3_required:
        logger.debug("ya-bin3 require set throught ya.conf, set handler_python_major_version = 3")
        handler_python_major_version = 3
    elif ya_bin3_required is False:
        logger.debug("ya-bin2 require set throught ya.conf, set handler_python_major_version = 2")
        handler_python_major_version = 2

    info = build.targets.resolve(source_root, old_targets)
    core.respawn.check_for_respawn(info.root, handler_python_major_version)

    kwargs['abs_targets'] = list(map(win_path_fix, info.targets))
    kwargs['rel_targets'] = [win_path_fix(os.path.relpath(x, info.root)) for x in info.targets]
    kwargs['arc_root'] = win_path_fix(info.root)

    if custom_build_directory:
        bld_dir = os.path.abspath(custom_build_directory)
    else:
        bld_dir = core.config.build_root()

    kwargs['bld_dir'] = win_path_fix(bld_dir)
    kwargs['bld_root'] = win_path_fix(build_root or os.path.join(bld_dir, 'build_root'))
    kwargs['custom_build_directory'] = win_path_fix(kwargs.get('bld_root'))  # XXX: deprecated

    if kwargs.get('output_root'):
        kwargs['output_root'] = win_path_fix(os.path.abspath(kwargs.get('output_root')))

    return core.yarg.Params(**kwargs)


def configure(params, with_respawn, handler_python_major_version):
    if with_respawn:
        yield resolve_and_respawn(params, handler_python_major_version)
    else:
        yield params
