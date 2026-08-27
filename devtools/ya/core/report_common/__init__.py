import getpass
import json
import logging
import platform
import sys
import time
import uuid

from devtools.ya.core import config
from devtools.ya.core import gsid
from devtools.ya.core import sec
from exts import flatten
from library.python import func
from library.python import strings

logger = logging.getLogger(__name__)


@func.lazy
def default_namespace():
    return 'yatool' + ('-dev' if config.is_developer_ya_version() else '')


@func.lazy
def get_distribution():
    if sys.version_info > (3, 7):
        import distro

        linux_distribution = '{} {} {}'.format(distro.name(), distro.version(), distro.codename()).strip()
    else:
        linux_distribution = ' '.join(platform.linux_distribution()).strip()
    windows_distribution = ' '.join(platform.win32_ver()).strip()
    mac_distribution = ' '.join(flatten.flatten(platform.mac_ver())).strip()
    return linux_distribution + windows_distribution + mac_distribution


class ReportEncoder(json.JSONEncoder):
    def default(self, obj):
        try:
            if isinstance(obj, set):
                obj_to_send = list(obj)
            else:
                obj_to_send = str(obj)
        except Exception:
            logger.exception("While converting %s", repr(obj))
            return super(ReportEncoder, self).default(obj)

        logger.debug(
            "Convert %s (%s) to `%s` (%s)",
            repr(obj),
            type(obj),
            repr(obj_to_send),
            type(obj_to_send),
        )
        return obj_to_send


@func.lazy
def system_info():
    return platform.system() + ' ' + platform.release() + ' ' + get_distribution()


def sanitize_value(value, suppressions):
    # type: (object, list[str]) -> object
    """Sanitize and normalize a telemetry value."""
    try:
        value = strings.unicodize_deep(value)
        return json.loads(sec.cleanup(json.dumps(value, cls=ReportEncoder), suppressions))
    except Exception as e:
        # Do not expose the input or traceback: they may contain a secret.
        return 'Unable to filter report value: {}'.format(e)


def create_event(key, value, namespace=default_namespace()):
    # type: (str, object, str) -> dict
    """Create a canonical telemetry envelope from a sanitized value."""
    return {
        '_id': uuid.uuid4().hex,
        'hostname': platform.node(),
        'user': getpass.getuser(),
        'platform_name': system_info(),
        'session_id': gsid.session_id(),
        'namespace': namespace,
        'key': key,
        'value': value,
        'timestamp': int(time.time()),
    }
