import os
import sys
import logging
import platform
import getpass
import uuid
import time
import json

from exts import func
from exts import flatten
from exts import strings
from core import config
from core import gsid


logger = logging.getLogger(__name__)
SUPPRESSIONS = None


class ReportTypes(object):
    EXECUTION = 'execution'
    RUN_YMAKE = 'run_ymake'
    FAILURE = 'failure'
    TIMEIT = 'timeit'
    LOCAL_YMAKE = 'local_ymake'
    HANDLER = 'handler'
    DIAGNOSTICS = 'diagnostics'
    WTF_ERRORS = 'wtf_errors'
    PROFILE_BY_TYPE = 'profile_by_type'
    PLATFORMS = 'platforms'
    VCS_INFO = 'vcs_info'
    PARAMETERS = 'parameters'
    YT_CACHE_ERROR = 'yt_cache_error'
    GRAPH_STATISTICS = 'graph_statistics'
    DISTBUILD_START_SCHEDULE_BUILD = 'distbuild_start_schedule_build'
    DISTBUILD_FINISH_SCHEDULE_BUILD = 'distbuild_finish_schedule_build'
    DISTBUILD_FINISH_BUILD = 'distbuild_finish_build'
    DISTBUILD_FINISH_DOWNLOAD_RESULTS = 'distbuild_finish_download_results'


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


def set_suppression_filter(suppressions):
    global SUPPRESSIONS
    SUPPRESSIONS = suppressions


class CompositeTelemetry:
    def __init__(self, backends=None):
        self._backends = backends or {}

    @property
    def no_backends(self):
        return len(self._backends) == 0

    def iter_backends(self):
        for name in self._backends:
            yield (name, self.get_backend_module(name))

    def get_backend_module(self, name):
        entry = self._backends.get(name)
        if hasattr(entry, '__call__'):
            entry = self._backends[name] = entry()
        return entry

    def report(self, key, value, namespace=default_namespace()):
        if self.no_backends:
            return

        def __filter(s):
            if SUPPRESSIONS:
                for sup in SUPPRESSIONS:
                    s = s.replace(sup, '[SECRET]')
                return s
            return s

        try:
            value = strings.unicodize_deep(value)
            svalue = json.loads(__filter(json.dumps(value, cls=ReportEncoder)))
        except Exception as e:
            # Don't expose exception: it may contain secret
            svalue = 'Unable to filter report value: {}'.format(e)

        logger.debug('Report %s: %s', key, svalue)
        for _, telemetry in self.iter_backends():
            telemetry.push(
                {
                    '_id': uuid.uuid4().hex,
                    'hostname': platform.node(),
                    'user': getpass.getuser(),
                    'platform_name': system_info(),
                    'session_id': gsid.session_id(),
                    'namespace': namespace,
                    'key': key,
                    'value': svalue,
                    'timestamp': int(time.time()),
                }
            )
        logger.debug('Reporting done')

    def init_reporter(self, shard='report', suppressions=None):
        if self.no_backends:
            return

        for telemetry_name, telemetry in self.iter_backends():
            telemetry.init(os.path.join(config.misc_root(), telemetry_name), shard)
        global SUPPRESSIONS
        SUPPRESSIONS = suppressions

    def request(self, tail, data, backend='snowden'):
        telemetry = self.get_backend_module(backend)
        if telemetry:
            return telemetry.request(tail, data)
        return "{}"


def gen_reporter():
    import app_config

    backends = {}

    if app_config.in_house:

        def load_snowden():
            from yalibrary import snowden

            return snowden

        backends['snowden'] = load_snowden

    return CompositeTelemetry(backends)


telemetry = gen_reporter()
