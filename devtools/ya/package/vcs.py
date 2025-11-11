import datetime
import json
import logging
import traceback
import re

import exts.func
import exts.tmp

from yalibrary.vcs import vcsversion
from yalibrary import vcs

logger = logging.getLogger(__name__)

UNDEFINED = 'undefined'
DEFAULT_DATE = '1970-01-01T00:00:00'


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
        return info.get('hash', info.get('revision', UNDEFINED))


class RevisionDate(VcsInfo):
    def calc(self, info):
        try:
            tstamp = info.get('date', DEFAULT_DATE)
            if tstamp == DEFAULT_DATE:
                logger.debug("Failed to obtain date from vcs. vcs info: %s", info)
            tstamp = datetime.datetime.fromisoformat(tstamp)
            tstamp = tstamp.date().isoformat()
            return tstamp
        except Exception:
            raise


class SvnRevision(VcsInfo):
    def calc(self, info):
        return info.get('revision') or self.get_last_change_revision(default=UNDEFINED)

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
    def __init__(self, arcadia_root, revision_means_trunk=True):
        super(Branch, self).__init__(arcadia_root)
        self.revision_means_trunk = revision_means_trunk

    def calc(self, info):
        branch = info.get('branch')
        if branch:
            vcs_type, _, _ = vcs.detect(cwd=self._arcadia_root)
            branch = branch.split('/')[-1]
            if vcs_type and vcs_type[0] == 'arc':
                if self.revision_means_trunk and info.get('revision') and re.match(r'^[0-9a-f]{40}$', branch):
                    branch = "trunk"
                else:
                    branch = branch.replace('_', '-')
        return branch
