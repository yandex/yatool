import logging

from devtools.ya.core.imprint import imprint
import devtools.ya.test.const as const
from library.python import unique_id

logger = logging.getLogger(__name__)


def get_test_result_uids(suites):
    test_uids = []
    for suite in suites:
        test_uids += suite.result_uids
    return test_uids


def get_uid(deps, prefix=None):
    # type: (list[str], None | str) -> str
    u = imprint.combine_imprints(*sorted(deps))
    if prefix:
        return prefix + const.UID_PREFIX_DELIMITER + u
    return u


def get_test_node_uid(params, prefix=None):
    u = imprint.combine_imprints(*params)
    if prefix:
        return prefix + const.UID_PREFIX_DELIMITER + u
    return u


def get_random_uid(prefix=None):
    return const.UID_PREFIX_DELIMITER.join([_f for _f in ["rnd", prefix, unique_id.gen16()] if _f])
