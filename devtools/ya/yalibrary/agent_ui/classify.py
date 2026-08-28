"""Naming of a run outcome for the agent.

The exit code is not derived here a second time: ya already chooses it once,
in configure_exit_code_definition (devtools/ya/app) for a run that died of an
exception and in YaMake._calc_exit_code for one that built and tested. This
module only gives that code the name ExitCodes gives it, plus the action an
agent is expected to take.
"""

import dataclasses

import devtools.ya.core.error as core_error


@dataclasses.dataclass(frozen=True)
class Outcome:
    exit_code: int | None
    exception: BaseException | None = None
    # Whether the run collected configure errors, observed rather than
    # derived from the exit code — see classify().
    configure_failed: bool = False


@dataclasses.dataclass(frozen=True)
class Verdict:
    category: str
    action: str


_CATEGORY_BY_EXIT_CODE = {
    core_error.ExitCodes.GENERIC_ERROR: 'generic_error',
    core_error.ExitCodes.UNHANDLED_EXCEPTION: 'unhandled_exception',
    core_error.ExitCodes.CONFIGURE_ERROR: 'configure_error',
    core_error.ExitCodes.NO_TESTS_COLLECTED: 'no_tests_collected',
    core_error.ExitCodes.TEST_FAILED: 'test_failed',
    core_error.ExitCodes.INFRASTRUCTURE_ERROR: 'infrastructure_error',
    core_error.ExitCodes.NOT_RETRIABLE_ERROR: 'not_retriable_error',
    core_error.ExitCodes.YT_STORE_FETCH_ERROR: 'yt_store_fetch_error',
    core_error.ExitCodes.USAGE_ERROR: 'usage_error',
}

# The action an agent is expected to take is a function of the category alone;
# it travels as a separate field for the agent's convenience.
_ACTION_BY_CATEGORY = {
    'generic_error': 'fix_code',
    'unhandled_exception': 'report',
    'configure_error': 'fix_makefile',
    'no_tests_collected': 'fix_command',
    'test_failed': 'fix_code',
    'infrastructure_error': 'rerun_as_is',
    # Not retriable is the opposite of rerun_as_is: the same run fails the same
    # way, so the error itself has to be looked at.
    'not_retriable_error': 'report',
    'yt_store_fetch_error': 'rerun_as_is',
    'usage_error': 'fix_command',
}


def classify(outcome: Outcome) -> Verdict | None:
    """Return the verdict for the outcome, or None when there is nothing to report."""
    if outcome.configure_failed:
        # A broken configuration is diagnosed by the fact that ya collected
        # configure errors, not by the exit code, because the code does not
        # report it yet: ignore_configure_errors still defaults to true, so
        # the run exits 1 without --keep-going and 0 with it, and
        # CONFIGURE_ERROR never arrives (see _calc_exit_code in
        # devtools/ya/build/ya_make.py and YA-1456). Reading the fact keeps
        # the advice identical before and after that default is flipped —
        # only exit_code changes — and closes the false green of a
        # --keep-going run whose configuration failed.
        category = 'configure_error'
    elif not outcome.exit_code:
        return None
    else:
        # A code ExitCodes does not name still failed the run; the result
        # events carry what exactly went wrong.
        category = _CATEGORY_BY_EXIT_CODE.get(outcome.exit_code, 'generic_error')
    return Verdict(category=category, action=_ACTION_BY_CATEGORY[category])
