import json
import logging

from devtools.ya.test import const

logger = logging.getLogger(__name__)


# Generate json mini report with chunk results for Distbuild
# For more info see DEVTOOLS-9618
def generate_report(chunk, filename):
    success_statuses = (
        const.Status.DESELECTED,
        const.Status.GOOD,
        const.Status.SKIPPED,
        const.Status.XFAIL,
        const.Status.XPASS,
    )

    if chunk.get_status() in success_statuses:
        succeed, failed = 1, 0
    else:
        succeed, failed = 0, 1

    for t in chunk.tests:
        if t.status in success_statuses:
            succeed += 1
        else:
            failed += 1

    res = {}
    if succeed:
        res['success'] = succeed
    if failed:
        res['fail'] = failed

    logger.debug("Result report: %s (%d tests, chunk status: %s)", res, len(chunk.tests), chunk.get_status())

    try:
        with open(filename, 'w') as afile:
            json.dump(res, afile)
    except Exception as e:
        logger.error("Failed to save result report: %s", e)
