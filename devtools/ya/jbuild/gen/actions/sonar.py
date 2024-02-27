import logging

import yalibrary.vcs

from devtools.ya.yalibrary.vcs import arc

logger = logging.getLogger(__name__)


def get_project_version_by_svn(opts):
    try:
        import yalibrary.svn as svn
    except ImportError:
        logger.debug('sonar: svn is not available')
        return None
    try:
        return str(svn.svn_info(opts.arc_root)['revision'])
    except Exception as e:
        logger.debug('sonar: svn info error %s', str(e))
    try:
        return str(svn.svn_version(opts.arc_root))
    except Exception as e:
        logger.debug('sonar: svnversion error %s', str(e))
    return None  # TODO try use hg || use uid as project version?


def get_project_version_by_arc(opts):
    try:
        arc_vcs = arc.Arc(opts.arc_root)
        info = arc_vcs.info()
    except Exception as e:
        logger.debug('sonar: can`t load arc info %s', str(e))
        return None
    return info.get("revision")


def get_project_version(opts):
    vcs_type = yalibrary.vcs.detect()[0][0]
    if vcs_type == "arc":
        return get_project_version_by_arc(opts)
    elif vcs_type == "svn":
        return get_project_version_by_svn(opts)
    else:
        return None
