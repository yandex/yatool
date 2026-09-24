import sys
import time

from yalibrary.status_view import helpers
from yalibrary.status_view import plain
from yalibrary import display
from yalibrary.term import size as term_size
import yalibrary.formatter

from . import pack

import typing as tp

if tp.TYPE_CHECKING:
    from yalibrary.status_view.status import Status  # noqa


def _calc_len(s):
    return len(display.strip_markup(s))


def _fmt_time(tm):
    tm = int(tm)
    if tm >= 60 * 1000:
        return str(tm / 60)
    if tm >= 60 * 100:
        return '{}:{}'.format(tm / 60, tm % 60 / 10)
    return '{}:{:02}'.format(tm / 60, tm % 60)


class TickThrottle(object):
    def __init__(self, func, interval):
        self._func = func
        self._interval = interval
        self._stamp = 0

    def tick(self, force=False, *args, **kwargs):
        cur_time = time.time()
        if force or cur_time > self._stamp + self._interval:
            self._func(*args, **kwargs)
            self._stamp = time.time()


STYLE_NINJA = 'ninja'
STYLE_MAKE = 'make'
STYLE_PLAIN = plain.STYLE


class TermView(object):
    def __init__(
        self,
        status,  # type: Status
        display,
        ninja=True,
        extra_progress=False,
        distbuild=False,
        output_replacements=None,
        patterns=None,
        use_roman_numerals=False,
        show_active_progress=True,
        style=None,
    ):
        # type: (...) -> None
        self._status = status
        self._display = display
        self._last_id = 0
        self._last_time = 0
        # `style` names the output style of the run; `ninja` is kept for the
        # callers that only choose between the rewritable status line and one
        # line per finished task.
        if style is None:
            style = STYLE_NINJA if ninja else STYLE_MAKE
        self._ninja = style == STYLE_NINJA
        self._start_time = time.time()
        # The plain style prints progress as messages; the strategy owns the
        # timing state and decides what, if anything, to print on a tick.
        self._progress = plain.PlainProgress(self._start_time) if style == STYLE_PLAIN else None
        self._extra_progress = extra_progress
        # save 2 chars for ^C
        self._max_len = term_size.termsize_or_default(sys.stderr, default=(25, 120))[1] - 2
        self._last_updated = 0
        self._default_status = (
            '[[c:yellow]]NO ACTIVE DISTBUILD TASKS[[rst]]' if distbuild else '[[c:yellow]]NO ACTIVE LOCAL TASKS[[rst]]'
        )
        self._roman_numerals = use_roman_numerals
        self._last_status = self._default_status_configuration()
        # TODO get rid of output_replacements and merge it with patterns
        self._output_replacements = output_replacements
        self._patterns = patterns
        self._show_active_progress = show_active_progress

    @staticmethod
    def _fmt(task):
        if hasattr(task, 'status'):
            return task.status()
        return None

    def _fmt_aux_task(self, task):
        lim = 50
        s = str(task)

        if len(s) < lim:
            return s
        return s[: lim - 3] + '...'

    def _fmt_status(self, task_status, pre, post):
        # Always trim to _max_len so the status line never wraps. Otherwise \r (carriage return)
        # only moves to the start of the current visual line, leaving previous wrapped parts
        # on screen and causing duplicated-looking output.
        separator = [1] if pre else []
        if isinstance(task_status, str):
            return pack.pack_status(
                pre + separator + [pack.Truncatable(task_status)] + post, _calc_len, self._max_len, trim=True
            )[0]

        ans = None
        for v in task_status:
            ans, ans_len = pack.pack_status(
                pre + separator + [pack.Truncatable(v)] + post, _calc_len, self._max_len, trim=True
            )
            break
        return ans

    def _fmt_body(self, body):
        # type: (str) -> str
        return helpers.format_body(body, self._patterns, self._output_replacements)

    def _emit_status(self, pre, task_status, post):
        if (pre, task_status, post) == self._last_status:
            return
        self._last_status = (pre, task_status, post)
        self._display.emit_status(self._fmt_status(task_status, pre, post))

    def _default_status_configuration(self, tag=None, post=None):
        if not tag:
            tag = self._default_status
        return (
            [
                '|[[unimp]]{}[[rst]]|'.format(
                    helpers.percent_to_string(100.0 * self._status.progress(), self._roman_numerals)
                )
            ],
            tag,
            post or [],
        )

    @staticmethod
    def _hidden(task):
        # type: (object) -> bool
        return hasattr(task, 'hide_me') and task.hide_me()

    def _body(self, task):
        # type: (object) -> str | None
        return self._fmt_body(task.body()) if hasattr(task, 'body') else None

    def _plain_finished(self, task):
        # type: (object) -> None
        """Report one finished task in the plain style: only its stderr, under a severity prefix."""
        task_status = self._fmt(task)
        if not task_status or self._hidden(task):
            return
        body = self._body(task)
        if not body:
            return
        # The stderr of a passed test is not a diagnosis of anything; the
        # test reporter prints the results.
        if not plain.is_failed(task) and plain.is_test(task):
            return
        self._display.emit_message(plain.line(plain.severity_of(task), plain.header(task_status)))
        self._display.emit_message(yalibrary.formatter.ansi_codes_to_markup(body))

    def _running(self, active):
        # type: (list) -> list
        """`(header, elapsed)` of all active tasks, longest running first."""
        # Status.active() is ordered by start time, the oldest task first.
        running = []
        for task, elapsed in reversed(active):
            task_status = self._fmt(task)
            # Auxiliary tasks (cache puts, node preparation) have no status: name them
            # as the terminal status line does, so the list is never shorter than `active`.
            running.append((plain.header(task_status) if task_status else str(task), elapsed))
        return running

    def _tick_plain(self):
        # type: () -> None
        """The tick of the plain style: finished tasks as messages, progress as the strategy decides."""
        for task in self._status.finished(self._last_id):
            self._last_id += 1
            self._plain_finished(task)
        active = self._status.active()
        line = self._progress.tick(
            time.time(), self._last_id, self._status.count, len(active), lambda: self._running(active)
        )
        if line:
            self._display.emit_message(line)

    def tick(self, *extra):
        if self._progress is not None:
            self._tick_plain()
            return

        if time.time() - self._last_updated > 1:
            self._max_len = term_size.termsize_or_default(sys.stderr, default=(25, 120))[1] - 2
            self._last_updated = time.time()
        self._last_time = time.time()
        finished = self._status.finished(self._last_id)
        for task in finished:
            task_status = self._fmt(task)
            tm = task.timing()[1] - self._start_time
            self._last_id += 1
            if not task_status:
                continue
            body = self._body(task)
            if not self._hidden(task) and (body or not self._ninja):
                pre = []
                if body:
                    pre.append('[[unimp]]-------[[rst]]')
                else:
                    pre.append(
                        '|[[unimp]]{}[[rst]]|'.format(
                            helpers.percent_to_string(
                                100.0 * self._last_id / max(1, self._status.count), self._roman_numerals
                            )
                        )
                    )
                if self._extra_progress:
                    pre.extend([1, '%s/%s' % (self._last_id, self._status.count)])

                self._display.emit_message(self._fmt_status(task_status, pre, []))
                if body:
                    self._display.emit_message(yalibrary.formatter.ansi_codes_to_markup(body))

        active = self._status.active()
        for task, tm in reversed(active):
            task_status = self._fmt(task)
            if not task_status:
                continue

            pre = []
            if self._show_active_progress:
                pre.append(
                    '|[[unimp]]{}[[rst]]|'.format(
                        helpers.percent_to_string(100.0 * self._status.progress(), self._roman_numerals)
                    )
                )
            post = []
            if tm > 10:
                post.extend([2, '[[bad]]{0:.1f}s[[rst]]'.format(tm)])
            post.append(-1)
            if len(active) > 1:
                post.extend(['[[unimp]]+{} more[[rst]]'.format(len(active) - 1), ' / '])
            post.extend(list(extra))

            self._emit_status(pre, task_status, post)
            return

        if self._ninja and len(active) != 0:
            post = [-1]
            if len(active) > 1:
                post.extend(['[[unimp]]+{} more[[rst]]'.format(len(active) - 1), ' / '])
            post.extend(list(extra))
            self._emit_status(
                *self._default_status_configuration(
                    '[[c:yellow]]AUXILIARY TASKS [[unimp]][{}][[rst]]'.format(self._fmt_aux_task(active[0][0])),
                    post=post,
                )
            )
            return

        if self._ninja:
            self._emit_status(*self._default_status_configuration())

    def finish(self):
        self.tick()
