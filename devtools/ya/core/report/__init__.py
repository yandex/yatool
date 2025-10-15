import collections
import os
import sys
import logging
import platform
import getpass
import uuid
import time
import json

import six

from exts import func
from exts import flatten
from exts import strings
from devtools.ya.core import config
from devtools.ya.core import gsid
from devtools.ya.core import sec


logger = logging.getLogger(__name__)
SUPPRESSIONS = []  # type: list[str]


class ReportTypes(object):
    ALL = 'all'  # <<< reserved for use in reports filter
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
    FAST_VCS_INFO_JSON = 'fast_vcs_info_json'
    FULL_VCS_INFO_JSON = 'full_vcs_info_json'
    PARAMETERS = 'parameters'
    YT_CACHE_ERROR = 'yt_cache_error'
    GRAPH_STATISTICS = 'graph_statistics'
    DISTBUILD = 'distbuild'
    YA_METRICS = 'ya_metrics'
    YT_CACHE_METRICS = 'yt_cache_metrics'
    UNIVERSAL_FETCHER = 'universal_fetcher'
    BUILD_ERRORS = 'build_errors'
    FINISH = 'finish'


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


@func.lazy
def compact_system_info():
    return platform.system() + ' ' + platform.machine()


def set_suppression_filter(suppressions):
    global SUPPRESSIONS
    SUPPRESSIONS = suppressions


class CompositeTelemetry:
    def __init__(self, backends=None):
        self._backends = backends or {}
        self._report_events = set()
        self._events_store = collections.defaultdict(list)

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

    def report(self, key, value, namespace=default_namespace(), urgent=False):
        if self.no_backends:
            return
        try:
            value = strings.unicodize_deep(value)
            svalue = json.loads(sec.cleanup(json.dumps(value, cls=ReportEncoder), SUPPRESSIONS))
        except Exception as e:
            # Don't expose exception: it may contain secret
            svalue = 'Unable to filter report value: {}'.format(e)

        if ReportTypes.ALL not in self._report_events and key not in self._report_events:
            logger.debug('Report_disabled %s: %s', key, svalue)  # log record for using in tests
            return

        self._events_store[key].append(svalue)

        logger.debug('Report%s %s: %s', ' urgent' if urgent else '', key, svalue)
        for telemetry_name, telemetry in self.iter_backends():
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
                },
                urgent=urgent,
            )
        logger.debug('Reporting done')

    def init_reporter(self, shard='report', suppressions=None, report_events=None):
        global SUPPRESSIONS
        if suppressions:
            SUPPRESSIONS = suppressions

        if self.no_backends:
            return

        if not report_events:
            self._report_events = set()
            logger.debug(
                "Skip init reporter shard=%s events=%s (no enabled reports)",
                shard,
                repr(self._report_events),
            )
            return

        self._report_events = report_events
        logger.debug(
            "Init reporter shard=%s events=%s",
            shard,
            repr(self._report_events),
        )

        for telemetry_name, telemetry in self.iter_backends():
            logger.debug("Initialize telemetry backend `%s`", telemetry_name)
            telemetry.init(os.path.join(config.misc_root(), telemetry_name), shard)
            logger.debug("Telemetry backend `%s` initialized", telemetry_name)

    def stop_reporter(self):
        if self.no_backends or not self._report_events:
            return

        for telemetry_name, telemetry in self.iter_backends():
            telemetry.stop()

    def request(self, tail, data, backend='snowden'):
        telemetry = self.get_backend_module(backend)
        if telemetry:
            return telemetry.request(tail, data)
        return "{}"

    def compose(self, *event_keys):
        res = {}
        for key in event_keys:
            if key in self._events_store:
                res[key] = self._events_store[key]

        return res


def mine_env_vars():
    def match(var):
        if var.startswith('YA_'):
            return True
        if var in ['TOOLCHAIN', 'CC', 'CXX', 'USER']:
            return True
        return False

    return dict((k, v) for k, v in six.iteritems(os.environ) if match(k))


def mine_cmd_args():
    return [strings.to_unicode(arg, strings.guess_default_encoding()) for arg in sys.argv]


def gen_reporter():
    import app_config

    backends = {}

    if app_config.in_house:

        def load_snowden():
            from yalibrary import snowden

            # TODO: Make this class, not module, for typing
            return snowden

        backends['snowden'] = load_snowden

    return CompositeTelemetry(backends)


telemetry = gen_reporter()
