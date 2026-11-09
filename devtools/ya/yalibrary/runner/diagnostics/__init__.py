"""Shared Python presentation rules for local build runners."""

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from enum import Enum

import six
from exts import shlex2

STDERR_CONTROL_PREFIX = '##'
_STATUS_PREFIX = '##status##'
_TAG_PREFIX = '##append_tag##'


class StderrControlKind(Enum):
    STATUS = 'status'
    TAG = 'tag'
    DISPLAY = 'display'


@dataclass(frozen=True)
class StderrControl:
    kind: StderrControlKind
    text: str


def is_stderr_control(line: str) -> bool:
    return line.startswith(STDERR_CONTROL_PREFIX)


def stderr_control(line: str, *, line_ending: str = '\n') -> StderrControl | None:
    """Classify a decoded line, retaining its reader's newline convention."""
    if line.startswith(_STATUS_PREFIX):
        return StderrControl(StderrControlKind.STATUS, line[len(_STATUS_PREFIX) :].rstrip(line_ending))
    if line.startswith(_TAG_PREFIX):
        return StderrControl(StderrControlKind.TAG, line[len(_TAG_PREFIX) :].strip())
    if is_stderr_control(line):
        return StderrControl(StderrControlKind.DISPLAY, line[len(STDERR_CONTROL_PREFIX) :].replace('|n', '\n'))
    return None


def format_command(argv: Sequence[str], environment: Mapping[str, str]) -> str:
    words = ['{}={}'.format(key, value) for key, value in environment.items()]
    words.extend(argv)
    return ' '.join(shlex2.quote(word) for word in words)


def format_command_failure(argv: Sequence[str | bytes], *, process_id: int, exit_code: int, cwd: str) -> str:
    return 'command (pid: {}) {} failed with exit code [[imp]]{}[[rst]] in {}'.format(
        process_id, ' '.join(six.ensure_str(word) for word in argv), exit_code, cwd
    )


def format_launch_error(error: OSError | str) -> str:
    return 'Process run failed: {}'.format(error)
