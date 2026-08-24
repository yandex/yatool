"""Pure projections of report entries into agent-facing events.

Entries are the post-processed dicts produced by ReportGenerator
(see devtools/ya/build/reports/autocheck_report.py) — the same dicts
JsonLineReport wraps as ``{"time", "type": "result", "data": entry}``.
"""

import os

from library.python import strings
from yalibrary.display import strip_markup

# Wire statuses that carry no failure information
# (see TestStatus in devtools/ya/test/reports/report_prototype.py).
_DROPPED_STATUSES = frozenset(("OK", "SKIPPED", "DISCOVERED"))

# (entry field, event field) pairs copied verbatim when present.
_FIELD_MAPPING = (
    ('type', 'kind'),
    ('path', 'path'),
    ('name', 'name'),
    ('subtest_name', 'subtest'),
    ('status', 'status'),
    ('error_type', 'error_type'),
    ('duration', 'duration'),
    ('toolchain', 'toolchain'),
)

_TEXT_LIMIT = 1000


def _entry_level(entry: dict) -> str | None:
    """Classify an entry as suite/chunk/test; build and configure entries have no level."""
    if entry.get('suite'):
        return 'suite'
    if entry.get('chunk'):
        return 'chunk'
    if entry.get('suite_hid') is not None:
        return 'test'
    return None


def _entry_text(entry: dict) -> str | None:
    """Derive a compact plain-text error description from the entry snippet."""
    if entry.get('error_type') == 'BROKEN_DEPS':
        # The root failure is its own event in the stream; forwarding the
        # snippet would repeat the same error once per dependent entry.
        return None

    # The trace-file `comment` of a test case is renamed to `rich-snippet`
    # when suites are flattened into report entries (see
    # make_suites_results_prototype in devtools/ya/test/reports/report_prototype.py).
    snippet = entry.get('rich-snippet')
    if not snippet:
        return None

    text = strip_markup(snippet).strip()
    if not text:
        return None
    # Keep the tail: build errors are prefixed with the whole compiler
    # command line while the diagnosis is at the end.
    return strings.truncate(text, _TEXT_LIMIT, whence=strings.Whence.Start)


def test_case_status(entry: dict) -> str | None:
    """Return the status of an individual test case entry, None for anything else.

    Unlike project_result, sees every status: passed tests are dropped from
    the event stream but still counted for the summary.
    """
    if entry.get('type') not in ('test', 'style'):
        return None
    if _entry_level(entry) != 'test':
        return None
    return entry.get('status')


def project_result(entry: dict) -> dict | None:
    """Project a report entry into a compact agent event.

    Returns None for entries that carry no failure information.
    """
    if entry.get('status') in _DROPPED_STATUSES:
        return None

    event = {'type': 'result'}
    for src, dst in _FIELD_MAPPING:
        value = entry.get(src)
        if value is not None:
            event[dst] = value

    level = _entry_level(entry)
    if level:
        event['level'] = level

    if level == 'suite':
        if entry.get('hid') is not None:
            event['suite_id'] = str(entry['hid'])
    elif level in ('chunk', 'test'):
        if entry.get('suite_hid') is not None:
            event['suite_id'] = str(entry['suite_hid'])

    if level == 'test' and entry.get('name') and entry.get('subtest_name'):
        # The exact string `ya test -F` accepts.
        event['test_name'] = f"{entry['name']}::{entry['subtest_name']}"

    links = entry.get('links')
    if links:
        event['logs'] = links
    if not links or entry.get('type') == 'style':
        # An agent reads logs by itself, so the text is forwarded only when
        # there are no logs to read. Style checks are the exception: their
        # snippet is the whole one-line diagnosis, so inlining it saves the
        # agent a trip to the log files.
        text = _entry_text(entry)
        if text:
            event['text'] = text

    return event


def project_progress(value: dict[str, list[dict]] | None, in_flight: int) -> dict | None:
    """Aggregate per-toolchain counters; ``in_flight`` is the count of tasks
    the runner is executing right now, present on every event so agents
    never handle its absence."""
    if not value:
        return None

    done_sum = 0
    total_sum = 0
    by_kind: dict[str, dict[str, int]] = {}
    for type_progresses in value.values():
        for type_progress in type_progresses:
            if type_progress.get('type') == 'configure':
                # YaMakeProgress configure counters are misleading (the total
                # starts at zero and done+total are bumped in batches); the
                # live configure stage has its own configure_* events instead.
                continue
            done = type_progress.get('done') or 0
            total = type_progress.get('total') or 0
            done_sum += done
            total_sum += total
            kind_acc = by_kind.setdefault(type_progress.get('type'), {'done': 0, 'total': 0})
            kind_acc['done'] += done
            kind_acc['total'] += total

    if not by_kind:
        return None

    return {'type': 'progress', 'done': done_sum, 'total': total_sum, 'in_flight': in_flight, 'by_kind': by_kind}


def project_running(active: list) -> dict | None:
    """Project the runner's active-task snapshot into a stall-hint payload.

    ``active`` is what Status.active() returns (see
    devtools/ya/yalibrary/status_view/status.py): (task, elapsed seconds)
    pairs. Tasks expose the same markup status line the human ticker shows;
    auxiliary tasks without one are skipped. The payload carries only the
    longest-running task — the one the stream appears to be stuck on — and
    the count of running tasks, so the hint stays small on wide builds.
    """
    entries = []
    for task, elapsed in active:
        status = task.status() if hasattr(task, 'status') else None
        if status and not isinstance(status, str):
            # RunNodeTask.status() returns a NodeView: an iterable of
            # rendering variants, most detailed first (see fmt_node in
            # devtools/ya/yalibrary/status_view/helpers.py). The human
            # ticker picks the first variant too.
            status = next(iter(status), None)
        if not status:
            continue
        text = strip_markup(status).strip()
        if not text:
            continue
        entries.append({'text': strings.truncate(text, _TEXT_LIMIT), 'elapsed': int(elapsed)})
    if not entries:
        return None
    return {'current_longest': max(entries, key=lambda entry: entry['elapsed']), 'total': len(entries)}


def plain_message_text(text: str) -> str | None:
    """Strip markup and truncate a free-form message, keeping the tail.

    The tail carries the diagnosis both in build errors (prefixed with the
    compiler command line) and in logged tracebacks.
    """
    text = strip_markup(text).strip()
    if not text:
        return None
    return strings.truncate(text, _TEXT_LIMIT, whence=strings.Whence.Start)


def _configure_error_path(where: str) -> str:
    """Normalize a ymake Where reference the way build reports do.

    Mirrors fix_dir in devtools/ya/build/ya_make.py: ``$S/dir/ya.make``
    becomes ``dir``, ``$B/dir/output`` becomes ``dir``.
    """
    if where.startswith('$S/'):
        where = where[len('$S/') :]
        if where.endswith('/ya.make'):
            where = where[: -len('/ya.make')]
    elif where.startswith('$B/'):
        where = os.path.dirname(where)[len('$B/') :]
    return where


def project_configure_error(event: dict) -> dict:
    """Project a buffered TDisplayMessage error into a configure result event.

    ``event`` carries the NEvent.TDisplayMessage keys: 'Type', 'Sub',
    'Message', 'Mod' and optionally 'Where', 'Row', 'Column', 'Platform'
    (see DisplayMessageSubscriber in devtools/ya/build/ya_make.py).

    The message comes from ymake with highlighting markup in it
    (``[[alt1]]PEERDIR[[rst]]``, written by hand in devtools/ymake and in
    the build/plugins configure-error strings); an agent reads plain text.
    """
    result = {'type': 'result', 'kind': 'configure', 'status': 'FAILED'}
    where = event.get('Where')
    if where:
        result['path'] = _configure_error_path(where)

    text = strip_markup(event.get('Message') or '').strip()
    if text:
        if 'Row' in event and 'Column' in event:
            text = f"{event['Row']}:{event['Column']}: {text}"
        result['text'] = strings.truncate(text, _TEXT_LIMIT)
    return result
