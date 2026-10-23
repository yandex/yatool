import base64
import json
import logging
import os
import re
import six
import sys
import typing as tp  # noqa

from library.python import func

logger = logging.getLogger(__name__)


TOKEN_RE = re.compile(r'AQAD-[a-zA-Z0-9_\-\\]+')
PRIVATE_KEY_PREFIX_RE = re.compile(r"-----BEGIN (RSA |EC |DSA |PGP )?PRIVATE KEY( BLOCK)?-----")
IN_CHECKS = ('access_key', 'secret_key')
ENDSWITH_CHECKS = (
    'token',
    'secret',
    'password',
    'passwd',
    '_pwd',  # not 'pwd': PWD/OLDPWD hold the working directory
    '_rsa',
    '_key',
    '_keys',
)
# Comma-separated names of env vars that hold secrets whatever their names are,
# e.g. yav/sb_vault REQUIREMENTS of a test suite exported by the Sandbox task that runs ya
SECRET_ENV_NAMES_VAR = 'YA_SECRET_ENV_NAMES'


class DoNotSearch(object):
    pass


def _secret_env_names():
    # type: () -> set[str]
    return {name.strip() for name in os.environ.get(SECRET_ENV_NAMES_VAR, '').split(',')}


# Please use one of public methods
@func.memoize()
def _search_secrets(key, value):
    # type: (tp.Optional[str], str) -> list[str]
    if not value:
        return []
    if not isinstance(value, six.string_types):
        return []
    if key and key in _secret_env_names():
        return [value]

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
        secrets.extend(_search_argv_secrets(k, v))

    return [cleanup(v, secrets) for v in cleanup_bazel_cache_data(argv)]


def _redact_bazel_caches(value):
    if isinstance(value, six.string_types):
        try:
            caches = json.loads(value)
        except ValueError:
            return '[SECRET]' if value.strip() else value
        hidden = _redact_cache_passwords(caches)
        return value if hidden == caches else json.dumps(hidden)
    return _redact_cache_passwords(value)


def _redact_cache_passwords(value):
    if isinstance(value, dict):
        return {
            key: (
                '[SECRET]'
                if key == 'password' and isinstance(item, six.string_types)
                else _redact_cache_passwords(item)
            )
            for key, item in six.iteritems(value)
        }
    if isinstance(value, (list, tuple)):
        return [_redact_cache_passwords(item) for item in value]
    return value


def cleanup_bazel_cache_data(value):
    """Hide cache passwords in log/report data without changing live build options."""
    if isinstance(value, dict):
        return {
            key: _redact_bazel_caches(item) if key == 'bazel_remote_caches' else cleanup_bazel_cache_data(item)
            for key, item in six.iteritems(value)
        }
    if not isinstance(value, (list, tuple)):
        return value

    hidden = []
    cache_value_follows = False
    option = '--bazel-remote-caches'
    for item in value:
        if isinstance(item, six.string_types):
            if cache_value_follows:
                item = _redact_bazel_caches(item)
            elif item.startswith(option + '='):
                item = option + '=' + _redact_bazel_caches(item[len(option) + 1 :])
            cache_value_follows = item == option
        else:
            item = cleanup_bazel_cache_data(item)
            cache_value_follows = False
        hidden.append(item)
    return hidden


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
            if sub_k == 'bazel_remote_caches':
                # Short cache passwords are hidden structurally; replacing them globally can corrupt JSON.
                for secrets in _iter_deep(sub_v):
                    yield [secret for secret in secrets if len(secret) >= 6]
                continue
            for secrets in _iter_deep(sub_k, sub_v):
                yield secrets
            for secrets in _iter_deep(k, sub_v):
                yield secrets
    else:
        yield _search_secrets(k, v)


def _encoded_secret_variants(value):
    variants = {value}
    # The password is encoded in the option JSON, then in a report or a log representation.
    for _ in range(2):
        for variant in list(variants):
            for ensure_ascii in (False, True):
                variants.add(json.dumps(variant, ensure_ascii=ensure_ascii)[1:-1])
            representation = repr(variant)
            if representation.startswith(('u"', "u'")):
                representation = representation[1:]
            variants.add(representation[1:-1])
            if representation.startswith('"'):
                # The surrounding JSON argument uses single quotes in a Python log representation.
                variants.add(representation[1:-1].replace("'", "\\'"))
    return variants


def _search_argv_secrets(key, value):
    secrets = set(_search_secrets(key, value))
    if not key or not value.strip() or key.lstrip('-').replace('-', '_') != 'bazel_remote_caches':
        return secrets

    try:
        caches = json.loads(value)
    except ValueError:
        # Invalid input is reported before option validation; do not expose its credentials.
        values = _search_secrets('password', value)
    else:
        # Keep public cache settings and report structure intact by hiding individual secrets.
        values = [secret for found in _iter_deep(caches) for secret in found]

    for secret in values:
        if len(secret) >= 6:
            secrets.update(_encoded_secret_variants(secret))
    return sorted(secrets, key=lambda secret: (-len(secret), secret))


def mine_suppression_filter(obj=None, argv=None, env=None):
    # type: (tp.Optional[tp.Any], list[str] | None | tp.Type[DoNotSearch], dict[str, tp.Any] | None | tp.Type[DoNotSearch]) -> list[str]
    secrets = set()

    if argv is not DoNotSearch:
        for k, v in _iter_argv(argv or sys.argv):
            secrets.update(_search_argv_secrets(k, v))

    if env is not DoNotSearch:
        for k, v in six.iteritems(env or os.environ):
            secrets.update(_search_secrets(k, v))

    secrets.update(*_iter_deep(obj))

    logger.debug("Found %d secrets", len(secrets))

    # YA-1248, in logs we see \\n if there was \n in token value
    escaped = {s.replace("\\n", "\\\\n") for s in secrets} | {s.replace("\n", "\\n") for s in secrets}
    secrets.update(escaped)

    logger.debug("Added %d variations for escaped secrets", len(secrets - escaped))

    return sorted(secrets)
