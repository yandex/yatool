import json
import logging
import traceback

import exts.func
import exts.tmp

from yalibrary.vcs import vcsversion

logger = logging.getLogger(__name__)


@exts.func.memoize()
def _get_vcs_info(source_root):
    return vcsversion.get_raw_version_info(source_root)


class VcsInfo(object):
    def __init__(self, arcadia_root):
        self._arcadia_root = arcadia_root

    def __str__(self):
        return str(self())

    def __format__(self, spec):
        return format(str(self), spec)

    @exts.func.memoize()
    def __call__(self):
        return self.calc(_get_vcs_info(self._arcadia_root))

    def calc(self, info):
        raise NotImplementedError


class Revision(VcsInfo):
    def calc(self, info):
        return info.get('hash', info.get('revision', 'undefined'))


class SvnRevision(VcsInfo):
    def calc(self, info):
        return info.get('revision') or self.get_last_change_revision(default='undefined')

    def get_last_change_revision(self, default):
        with exts.tmp.temp_dir() as tmpdir:
            try:
                line = vcsversion.get_version_info(self._arcadia_root, tmpdir)
                data = json.loads(line)
                return data.get('ARCADIA_SOURCE_LAST_CHANGE', default)
            except Exception:
                logger.debug("Can't extract svn revision from arc repo: %s", traceback.format_stack())
                return default


class Branch(VcsInfo):
    def calc(self, info):
        branch = info.get('branch')
        if branch:
            vcs_type, _, _ = vcsversion.detect(cwd=self._arcadia_root)
            branch = branch.split('/')[-1]
            if vcs_type and vcs_type[0] == 'arc':
                branch = branch.replace('_', '-')
        return branch
