import itertools
import logging
import os
import sys

import library.python.find_root


logger = logging.getLogger(__name__)


def detect(paths=None, cwd=None, check_tar=None):
    cwd = cwd or os.getcwd()
    logger.debug('detecting vcs from %s for paths: %s', cwd, paths)
    if paths:
        leafs = [os.path.join(cwd, path) for path in paths]
        common_root = lcp(leafs)
    else:
        common_root = cwd
    logger.debug('common root: %s', common_root)

    mem = {}

    def detect_vcs_root(path):
        types = []
        if os.path.isdir(os.path.join(path, '.svn')):
            types.append('svn')
        if os.path.isdir(os.path.join(path, '.arc')) and os.path.isfile(os.path.join(path, '.arc', 'HEAD')):
            types.append('arc')
        if os.path.isdir(os.path.join(path, '.hg')):
            types.append('hg')
        if os.path.isdir(os.path.join(path, '.git')):
            types.append('git')
        if check_tar and os.path.isfile(os.path.join(path, '__SVNVERSION__')):
            types.append('tar')
        mem['types'] = types
        return len(types) != 0

    vcs_root = library.python.find_root.detect_root(common_root, detect_vcs_root)
    logger.debug('vcs root: %s (%s)', vcs_root, ' '.join(mem['types']))

    return tuple(mem.get('types', [])), vcs_root, common_root


def detect_vcs_type(paths=None, cwd=None, check_tar=None):
    return detect(paths, cwd, check_tar)[0][0]


def lcp(paths):
    paths = [tuple(path.split(os.path.sep)) for path in paths]
    if sys.version_info > (3, 0):
        zzip = zip
    else:
        zzip = itertools.izip
    ret = tuple(x[0] for x in itertools.takewhile(lambda x: len(frozenset(x)) == 1, zzip(*paths)))
    return os.path.sep.join(ret)
