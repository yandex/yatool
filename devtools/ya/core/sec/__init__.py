import base64
from collections import namedtuple
import os
import re
import six
import sys
import logging

import typing as tp  # noqa

from exts import func


logger = logging.getLogger(__name__)


TOKEN_PREFIX = 'AQAD-'
PRIVATE_KEY_PREFIX_RE = re.compile(r"-----BEGIN (RSA |EC |DSA )?PRIVATE KEY-----")
IN_CHECKS = ('access_key',)
ENDSWITH_CHECKS = (
    'token',
    'secret',
    'password',
    '_rsa',
)

SupressedInfo = namedtuple("SupressedInfo", ["suppressed", "need_to_be_supressed", "original_value"])


class DoNotSearch(object):
    pass


# Please use one of public methods
def _may_be_token(key, value):
    # type: (tp.Optional[str], str) -> bool
    if not value:
        return False
    if not isinstance(value, six.string_types):
        return False

    if TOKEN_PREFIX in value:
        return True

    if len(value) < 6 and (value.islower() or value.isalpha()):
        # Euristic for short text value, f.e. `token`
        return False

    if key:
        _key_lower = key.lower()

        if _key_lower.endswith(ENDSWITH_CHECKS) or any((key_part in _key_lower for key_part in IN_CHECKS)):
            try:
                if six.ensure_text(value).isdecimal() and len(value) < 10:
                    # We treat short enough (10^10 ~= 2^30) numbers as non-secrets
                    # This should be enough to avoid reasonable numbers in variables
                    return False
            except BaseException:
                pass
            return True

    if PRIVATE_KEY_PREFIX_RE.search(value):
        return True

    # detect aws keys
    try:
        if b"aws" in base64.b64decode(value):
            return True
    except Exception:
        pass

    return False


@func.memoize()
def _suppress(key, value):
    """Check key-value pair, test it for token memoize and return complex info about supressed value"""
    # type: (str, tp.Any) -> SupressedInfo

    suppressed = value  # type: str
    need_to_be_supressed = False  # type: bool
    original_value = value  # type: str

    if _may_be_token(key, value):
        need_to_be_supressed = True
        suppressed = "[SECRET]"

    return SupressedInfo(suppressed, need_to_be_supressed, original_value)


def cleanup(s, suppressions, replacement='[SECRET]'):
    # type: (str, tp.Sequence[str], str) -> str
    """Cleanup string from any secrets"""
    if suppressions:
        for sup in suppressions:
            if sup in s:
                s = s.replace(sup, replacement)
    return s


def environ(env=None):
    # type: (dict[str, str] | None) -> dict[str, str]
    """Return env without tokens and secrets"""
    return dict((k, _suppress(k, v).suppressed) for k, v in six.iteritems(env or os.environ))


def argv(argv=None):
    # type: (list[str] | None) -> list[str]
    """Return argv without tokens and secrets"""
    secrets = mine_suppression_filter(argv=argv, env=DoNotSearch)

    return [cleanup(arg, secrets) for arg in (argv or sys.argv)]


def _iter_argv(argv=None):
    # type: (list[str] | None | tp.Type[DoNotSearch]) -> tp.Iterable[tuple[str | None, str]]
    possible_key = None

    if argv is DoNotSearch:
        return

    for arg in argv or sys.argv:
        if arg.startswith("--"):
            if '=' in arg:
                k, v = arg.split("=", 1)
                yield k, v
                possible_key = None
            else:
                possible_key = arg[2:]
        else:
            yield possible_key, arg
            possible_key = arg


def _iter_env(env=None):
    # type: (dict[str, tp.Any] | None | tp.Type[DoNotSearch]) -> tp.Iterable[tuple[str | None, str]]

    if env is DoNotSearch:
        return

    for env_key, env_value in six.iteritems(env or os.environ):
        yield env_key, env_value


def _iter_argv_and_env(argv=None, env=None):
    # type: (list[str] | None | tp.Type[DoNotSearch], dict[str, tp.Any] | None | tp.Type[DoNotSearch]) -> tp.Iterable[tuple[str | None, str]]
    for _ in _iter_argv(argv):
        yield _

    for _ in _iter_env(env):
        yield _


def iter_deep(*args):
    # type: (tp.Any | None) -> tp.Iterable[SupressedInfo]

    assert 0 < len(args) <= 2, "Can't process {} args, use `iter_deep(v)` or `iter_deep(k, v)`".format(len(args))

    if len(args) == 1:
        v = args
        k = None
    elif len(args) == 2:
        k, v = args

    if v is None:
        return

    if isinstance(v, (tuple, list, set)):
        for sub_v in v:
            for info in iter_deep(k, sub_v):
                yield info
    elif isinstance(v, dict):
        for sub_k, sub_v in six.iteritems(v):
            for info in iter_deep(sub_k, sub_v):
                yield info
            for info in iter_deep(k, sub_v):
                yield info
    else:
        info = _suppress(k, v)
        if info.need_to_be_supressed:
            yield info.original_value


def mine_suppression_filter(obj=None, argv=None, env=None):
    # type: (tp.Optional[tp.Any], list[str] | None | tp.Type[DoNotSearch], dict[str, tp.Any] | None | tp.Type[DoNotSearch]) -> list[str]
    secrets = set()

    for key, value in _iter_argv_and_env(argv=argv, env=env):
        info = _suppress(key, value)

        if info.need_to_be_supressed:
            secrets.add(info.original_value)

    secrets.update(iter_deep(obj))

    # YA-1248, in logs we see \\n if there was \n in token value
    escaped = {s.replace("\\n", "\\\\n") for s in secrets}
    secrets.update(escaped)

    logger.debug("Found %d secrets", len(secrets))

    return sorted(secrets)
