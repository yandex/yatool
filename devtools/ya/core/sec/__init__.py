import base64
import logging
import os
import re
import six
import sys
import typing as tp  # noqa

from exts import func


logger = logging.getLogger(__name__)


TOKEN_RE = re.compile(r'AQAD-[a-zA-Z0-9_\-\\]+')
PRIVATE_KEY_PREFIX_RE = re.compile(r"-----BEGIN (RSA |EC |DSA |PGP )?PRIVATE KEY( BLOCK)?-----")
IN_CHECKS = ('access_key',)
ENDSWITH_CHECKS = (
    'token',
    'secret',
    'password',
    '_rsa',
)


class DoNotSearch(object):
    pass


# Please use one of public methods
@func.memoize()
def _search_secrets(key, value):
    # type: (tp.Optional[str], str) -> list[str]
    if not value:
        return []
    if not isinstance(value, six.string_types):
        return []

    if len(value) < 6 and (value.islower() or value.isalpha()):
        # Euristic for short text value, f.e. `token`
        return []

    if key:
        _key_lower = key.lower()

        if _key_lower.endswith(ENDSWITH_CHECKS) or any((key_part in _key_lower for key_part in IN_CHECKS)):
            try:
                if six.ensure_text(value).isdecimal() and len(value) < 10:
                    # We treat short enough (10^10 ~= 2^30) numbers as non-secrets
                    # This should be enough to avoid reasonable numbers in variables
                    return []
            except BaseException:
                pass
            return [value]

    if PRIVATE_KEY_PREFIX_RE.search(value):
        return [value]

    # detect aws keys
    try:
        if b"aws" in base64.b64decode(value):
            return [value]
    except Exception:
        pass

    return list(set(re.findall(TOKEN_RE, value)))


def cleanup(s, suppressions, replacement='[SECRET]'):
    # type: (str, tp.Sequence[str], str) -> str
    """Cleanup string from any secrets"""
    for sup in suppressions:
        s = s.replace(sup, replacement)
    return s


def environ(env=None):
    # type: (dict[str, str] | None) -> dict[str, str]
    """Return env without tokens and secrets"""
    return {k: cleanup(v, _search_secrets(k, v)) for k, v in six.iteritems(env or os.environ)}


def argv(argv=None):
    # type: (list[str] | None) -> list[str]
    """Return argv without tokens and secrets"""
    argv = argv or sys.argv
    secrets = []

    for k, v in _iter_argv(argv):
        secrets.extend(_search_secrets(k, v))

    return [cleanup(v, secrets) for v in argv]


def _iter_argv(argv):
    # type: (list[str]) -> tp.Iterable[tuple[str | None, str]]
    possible_key = None

    for arg in argv:
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


def _iter_deep(*args):
    # type: (tp.Any | None) -> tp.Iterable[list[str]]

    assert 0 < len(args) <= 2, "Can't process {} args, use `_iter_deep(v)` or `_iter_deep(k, v)`".format(len(args))

    k, v = (None, args) if len(args) == 1 else args

    if v is None:
        return

    if isinstance(v, (tuple, list, set)):
        for sub_v in v:
            for secrets in _iter_deep(k, sub_v):
                yield secrets
    elif isinstance(v, dict):
        for sub_k, sub_v in six.iteritems(v):
            for secrets in _iter_deep(sub_k, sub_v):
                yield secrets
            for secrets in _iter_deep(k, sub_v):
                yield secrets
    else:
        yield _search_secrets(k, v)


def mine_suppression_filter(obj=None, argv=None, env=None):
    # type: (tp.Optional[tp.Any], list[str] | None | tp.Type[DoNotSearch], dict[str, tp.Any] | None | tp.Type[DoNotSearch]) -> list[str]
    secrets = set()

    if argv is not DoNotSearch:
        for k, v in _iter_argv(argv or sys.argv):
            secrets.update(_search_secrets(k, v))

    if env is not DoNotSearch:
        for k, v in six.iteritems(env or os.environ):
            secrets.update(_search_secrets(k, v))

    secrets.update(*_iter_deep(obj))

    # YA-1248, in logs we see \\n if there was \n in token value
    escaped = {s.replace("\\n", "\\\\n") for s in secrets}
    secrets.update(escaped)

    logger.debug("Found %d secrets", len(secrets))

    return sorted(secrets)
