import errno
import logging
import os
import os.path
import filecmp
import stat

import exts.fs
from library.python import windows
import six
from six.moves import xrange
from devtools.ya.build.build_opts import ClonefileMode

logger = logging.getLogger(__name__)

__selected_clonefile_mode = ClonefileMode.no


def write_into_file(src, data):
    with open(src, 'wb') as f:
        f.write(six.ensure_binary(data))


def read_from_file(src):
    with open(src, 'rb') as f:
        return f.read()


def prepare_dir(path):
    if not os.path.lexists(path):
        exts.fs.create_dirs(path)
    elif not os.path.isdir(path):
        logger.warning('Can\'t create directory %s: not a directory', path)
        return False

    return True


def prepare_parent_dir(link):
    assert link

    link_dir = os.path.dirname(link)
    if link_dir:
        return prepare_dir(link_dir)

    return True


def clone_file_idle(src, dst):
    return False


def clone_file_copy(target, link):
    exts.fs.copy2(target, link)
    return True


def clone_file_real(target, link):
    try:
        return exts.fs.macos_clone_file(target, link)
    except Exception as e:
        logger.warning('Unable to clone %s to %s: %r', target, link, e)
        return False


def clone_file_with_copy_fallback(target, link):
    return clone_file_real(target, link) or clone_file_copy(target, link)


clone_file = clone_file_idle


def enable_clonefile():
    assert exts.fs.macos_clone_file
    global clone_file
    clone_file = clone_file_real


def enable_clonefile_with_copy_fallback():
    assert exts.fs.macos_clone_file
    global clone_file
    clone_file = clone_file_with_copy_fallback


def enable_clonefile_copy():
    global clone_file
    clone_file = clone_file_copy


def set_clonefile_mode(mode):
    global __selected_clonefile_mode
    global clone_file
    __selected_clonefile_mode = mode
    if mode == ClonefileMode.full:
        enable_clonefile()
    elif mode == ClonefileMode.mixed:
        enable_clonefile_with_copy_fallback()
    else:
        clone_file = clone_file_idle


def make_hardlink(target, link, retries=10, prepare=False):
    assert target and link

    for i in reversed(xrange(retries)):
        try:
            if prepare:
                prepare_parent_dir(link)
                exts.fs.ensure_removed(link)
            return exts.fs.hardlink_or_copy(target, link)
        except OSError:
            if i == 0:
                logger.warning('Can\'t create hardlink or copy of %s as %s', target, link)
                raise

            prepare_parent_dir(link)
            exts.fs.ensure_removed(link)

    raise RuntimeError('Invalid state')


def __needs_cloning(target):
    try:
        st_mode = os.stat(target).st_mode
    except OSError:
        return False
    return (st_mode & stat.S_IXUSR) != 0


def make_clone_or_hardlink(target, link, retries=10, prepare=False, prefer_clone=False):
    if (__selected_clonefile_mode == ClonefileMode.full) or (
        __selected_clonefile_mode == ClonefileMode.mixed and (prefer_clone or __needs_cloning(target))
    ):
        if prepare:
            prepare_parent_dir(link)
            exts.fs.ensure_removed(link)
        if clone_file(target, link):
            return True
    return make_hardlink(target, link, retries=retries, prepare=prepare)


@windows.win_disabled
def make_symlink(target, link, prepare=True, warn_is_file=True, compare_file=False):
    assert target and link

    if os.path.exists(link) and not os.path.islink(link):
        if warn_is_file and compare_file and filecmp.cmp(target, link, shallow=False):
            warn_is_file = False
        log_problem = logger.warning if warn_is_file else logger.debug
        log_problem('Can\'t create symlink to %s: %s exists and it\'s not a symlink', target, link)
        return

    if prepare:
        prepare_parent_dir(link)
        exts.fs.ensure_removed(link)

    try:
        exts.fs.symlink(target, link)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

    if not os.path.islink(link):
        log_problem = logger.warning if warn_is_file else logger.debug
        log_problem('Failed to create symlink %s to %s, got not a link', link, target)
