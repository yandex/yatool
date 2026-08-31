"""Naming of a run outcome for the agent.

The exit code is not derived here a second time: ya already chooses it once,
in configure_exit_code_definition (devtools/ya/app) for a run that died of an
exception and in YaMake._calc_exit_code for one that built and tested. This
module only gives that code the name ExitCodes gives it, plus the advice an
agent needs to act on it.
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
    advice: str


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

# What to do about the outcome is a function of the category alone; it travels
# as a separate field so that the agent does not have to know the table. The
# advice is prose rather than a slug: a slug would only repeat the category,
# while the agent's next step turns on things a name cannot hold — where the
# failure can hide, which events carry the diagnosis, whether a rerun is worth
# anything.
_ADVICE_BY_CATEGORY = {
    'generic_error': (
        "The run failed without an exit code of its own. The failed build and test events of this stream, "
        "and the `text` field of this summary, carry the diagnosis — read them and fix what they name. "
        "Rerunning the same command unchanged fails the same way."
    ),
    'unhandled_exception': (
        "ya itself crashed: the failure is in the build system, not in the code being built. The `text` field "
        "holds the exception, the full traceback is in the ya log (`$YA_CACHE_DIR/logs`, `~/.ya/logs` by "
        "default). Do not rework the command around the crash — report it with the log."
    ),
    'configure_error': (
        "Configuration failed, so the build description has to be fixed before anything else. The error is not "
        "necessarily in the ya.make of the target: it can come from any file the configuration pulls in — an "
        ".inc, a macro, or the ya.make of a module reached through PEERDIR or RECURSE. Read the configure "
        "events of this stream for the file and the line they name instead of assuming the target's own "
        "ya.make. Build and test failures collected under a broken configuration may disappear once it is "
        "fixed, so fix the configuration first and rerun."
    ),
    'no_tests_collected': (
        "Tests were requested but none was collected, so nothing ran and nothing is known to be broken. What "
        "needs fixing is the command, not the code: check the target path, the `-F` filter, and the test sizes "
        "the run allows (`-t`, `-tt`, `-ttt`). `ya test -L` on the target lists what there is to run."
    ),
    'test_failed': (
        "Tests ran and some of them failed. Every failure is a separate event of this stream carrying `path`, "
        "`name` and the `text` of the failure — fix the code or the test they name. A rerun of the same "
        "command fails the same way, so change something before rerunning."
    ),
    'infrastructure_error': (
        "The run died of an error ya treats as temporary — network, disk space, a service that was briefly "
        "unavailable — not of anything in the code. Rerun the same command as is. If the failure repeats, the "
        "environment is what to look at (free space, network access, tokens), still not the command."
    ),
    # Not retriable is the opposite of infrastructure_error: the same run fails
    # the same way, so the error itself has to be looked at.
    'not_retriable_error': (
        "The run died of an error explicitly marked as not retriable: the same command fails the same way, so "
        "a rerun buys nothing. Read the `text` field of this summary — if it names something the command or "
        "the code can fix, fix that; otherwise report the failure together with the ya log."
    ),
    'yt_store_fetch_error': (
        "A node could not be fetched from the distributed cache; the command itself is fine. Rerun it as is — "
        "the cache is usually reachable on the next attempt. If it keeps failing, `--no-yt-store` gets the run "
        "through by building everything locally."
    ),
    'usage_error': (
        "ya rejected the command line itself, so nothing was configured or built: an unknown option, a value "
        "it does not accept, or a target that is not a path in the repository. Fix the invocation — "
        "`ya <subcommand> --help` lists what is accepted — and leave the code alone."
    ),
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
    return Verdict(category=category, advice=_ADVICE_BY_CATEGORY[category])
