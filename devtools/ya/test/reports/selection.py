# coding: utf-8
"""Selection policy shared by the test reporters: what is worth showing."""

import re

from devtools.ya.test import const

_RUN_LOG = re.compile(r"_run\d+$")


def select_test_cases(chunk, omitted_statuses, show_passed, show_deselected, show_skipped):
    # type: (tp.Any, set, bool, bool, bool) -> list
    """Test cases of the chunk that pass the status filters, in the chunk order."""
    selected = []
    for test_case in chunk.tests:
        if test_case.status in omitted_statuses and not show_passed:
            continue
        if test_case.status == const.Status.DESELECTED and not show_deselected:
            continue
        if test_case.status == const.Status.SKIPPED and not show_skipped:
            continue
        selected.append(test_case)
    return selected


def significant_logs(logs):
    # type: (dict) -> dict
    """Logs without the per-run copies: those are local and reachable from the main ones."""
    return {name: path for name, path in logs.items() if not _RUN_LOG.search(name)}
