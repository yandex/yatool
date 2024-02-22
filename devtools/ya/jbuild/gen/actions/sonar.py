import json
import logging
import subprocess

import yalibrary.tools as tools
import yalibrary.vcs

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


# TODO move to yalibrary/vcs/arc after https://a.yandex-team.ru/review/5251852/details
def arc_info(arc_root):
    arc_bin = tools.tool("arc")
    res = subprocess.check_output([arc_bin, "info", "--json"], cwd=arc_root)
    return json.loads(res)


def get_project_version_by_arc(opts):
    try:
        info = arc_info(opts.arc_root)
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
