import datetime
import logging
import re

from yalibrary.vcs import vcsversion
from yalibrary import vcs

logger = logging.getLogger(__name__)

UNDEFINED = 'undefined'
DEFAULT_DATE = '1970-01-01T00:00:00'


class VcsInfo(object):
    _require_slow = False

    def __init__(self, arcadia_root, force_vcs_info_update=False):
        self._arcadia_root = arcadia_root
        self._force_vcs_info_update = force_vcs_info_update

    def __str__(self):
        return str(self())

    def __format__(self, spec):
        return format(str(self), spec)

    def __call__(self):
        return self.calc(
            vcsversion.VcsInfo(self._arcadia_root).get_info(
                require_slow=self._require_slow or self._force_vcs_info_update,
                raise_on_failure=self._force_vcs_info_update,
            )
        )

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
    _require_slow = True

    def calc(self, info):
        # type: (dict) -> object
        return info.get('svn_commit_revision', UNDEFINED)


class Branch(VcsInfo):
    def __init__(self, arcadia_root, revision_means_trunk=True, force_vcs_info_update=False):
        super(Branch, self).__init__(arcadia_root, force_vcs_info_update=force_vcs_info_update)
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
