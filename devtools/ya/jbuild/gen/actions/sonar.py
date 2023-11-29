import logging

logger = logging.getLogger(__name__)


def get_project_version(opts):
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
