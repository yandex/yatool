"""Pure projections of report entries into agent-facing events.

Entries are the post-processed dicts produced by ReportGenerator
(see devtools/ya/build/reports/autocheck_report.py) — the same dicts
JsonLineReport wraps as ``{"time", "type": "result", "data": entry}``.
"""

import os

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

    # Forwarded whole: the snippet is already bounded upstream by
    # REPORT_SNIPPET_LIMIT (see truncate_snippet in
    # devtools/ya/build/reports/utils.py), which cuts the middle and keeps
    # both ends. Cutting again here would drop one of them — for javac that
    # is the `error:` lines, which come before the warnings and the summary.
    return strip_markup(snippet).strip() or None


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


def is_configure_failure(entry: dict) -> bool:
    """Tell a report entry that carries a configuration failure from any other.

    Configure entries are produced for every target (see
    add_configure_results in devtools/ya/build/reports/autocheck_report.py);
    only the ones that collected errors are marked failed.
    """
    return entry.get('type') == 'configure' and entry.get('status') == 'FAILED'


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
        entries.append({'text': text, 'elapsed': int(elapsed)})
    if not entries:
        return None
    return {'current_longest': max(entries, key=lambda entry: entry['elapsed']), 'total': len(entries)}


def plain_message_text(text: str) -> str | None:
    """Strip markup off a free-form message and forward it whole."""
    return strip_markup(text).strip() or None


def _configure_path(where: str) -> str:
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


def _configure_message_text(event: dict) -> str | None:
    """Plain-text body of a TDisplayMessage, prefixed with its position.

    The message comes from ymake with highlighting markup in it
    (``[[alt1]]PEERDIR[[rst]]``, written by hand in devtools/ymake and in
    the build/plugins configure-error strings); an agent reads plain text.
    """
    text = strip_markup(event.get('Message') or '').strip()
    if not text:
        return None
    if 'Row' in event and 'Column' in event:
        text = f"{event['Row']}:{event['Column']}: {text}"
    return text


def project_configure_error(event: dict) -> dict:
    """Project a buffered TDisplayMessage error into a configure result event.

    ``event`` carries the NEvent.TDisplayMessage keys: 'Type', 'Sub',
    'Message', 'Mod' and optionally 'Where', 'Row', 'Column', 'Platform'
    (see DisplayMessageSubscriber in devtools/ya/build/ya_make.py).
    """
    result = {'type': 'result', 'kind': 'configure', 'status': 'FAILED'}
    where = event.get('Where')
    if where:
        result['path'] = _configure_path(where)

    text = _configure_message_text(event)
    if text:
        result['text'] = text
    return result


def project_configure_warning(event: dict) -> dict | None:
    """Project a TDisplayMessage warning into a configure_warning event.

    Its own event type, next to configure_started/progress/finished: a
    configure warning does not fail the build, so it is not a ``result``
    (those stay synonymous with "something failed"), and it belongs to the
    configure stage rather than to the free-form ``message`` log stream.
    An agent that only wants failures filters it out by type alone.

    Returns None for a message that carries no text.
    """
    text = _configure_message_text(event)
    if not text:
        return None

    result = {'type': 'configure_warning'}
    where = event.get('Where')
    if where:
        result['path'] = _configure_path(where)
    result['text'] = text
    return result
