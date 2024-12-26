"""
Fetches resources from sandbox
"""

import collections
import logging
import optparse
import os
import signal
import sys

import app_config
import devtools.ya.core.error
import devtools.ya.test.util.shared
from devtools.ya.test.dependency import sandbox_storage

logger = logging.getLogger(__name__)


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--storage-root", dest="build_root", help="Storage route", action='store')
    parser.add_option("--id", dest="resource_id", help="Resource id to fetch", action='store')
    parser.add_option("--resource-file", dest="resource_file", help="Downloaded resource file", action='store')
    parser.add_option("--rename-to", help="Rename downloaded resource to", action='store')
    parser.add_option(
        "--for-dist-build", dest="for_dist_build", help="To run on dist build", action='store_true', default=False
    )
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option("--custom-fetcher", help="custom fetcher", action='store')
    parser.add_option("--token", help="ya tool token ", action='store')
    parser.add_option("--token-path", help="file with ya tool token ", action='store')
    parser.add_option("--dir-output-tared-path", help="path to dir_output tar", action='store', default=None)
    parser.add_option("--timeout", type=int, default=0)
    parser.add_option(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )

    return parser.parse_args()


def compact_resinfo(res):
    def split(s):
        i = 0
        for i, c in enumerate(reversed(s)):
            if not c.isdigit():
                break
        if i:
            return s[:-i], s[-i:]
        return s, None

    def fmt(p, s):
        if len(s) > 1:
            return "%s{%s}" % (p, ','.join(s))
        if s[0] is None:
            return p
        return p + s[0]

    def compact_sources(s):
        d = collections.defaultdict(list)
        for prefix, suffix in (split(x) for x in sorted(s)):
            d[prefix].append(suffix)
        return [fmt(k, v) for k, v in d.items()]

    try:
        resinfo = dict(res._info)
        resinfo["sources"] = compact_sources(resinfo["sources"])
        for k, v in resinfo.items():
            if isinstance(v, dict) and v.get('links'):
                v['links'] = v['links'][:10]
        return sandbox_storage.StoredResourceInfo(resinfo)
    except Exception as e:
        logger.error("Failed to compact sandbox sources: %s", e)
        return res


def sigalrm_handler(s, f):
    os.abort()


def setup_timeout(timeout):
    if not timeout:
        logger.debug("Download node is not limited by time")
        return

    timeout = max(timeout - 20, int(timeout * 0.95))
    try:
        signal.signal(signal.SIGALRM, sigalrm_handler)
        signal.alarm(timeout)
    except Exception as e:
        logger.exception("Failed to setup timeout for node: %s", e)
    else:
        logger.debug("Set an alarm clock with %ds delay", timeout)


def main():
    options, _ = get_options()
    devtools.ya.test.util.shared.setup_logging(options.log_level, options.log_path)

    setup_timeout(options.timeout)
    build_root = options.build_root
    resource_id = options.resource_id
    token = devtools.ya.test.util.shared.get_oauth_token(options)
    try:
        # XXX static resource info is provided from a resource mapping in case of OSS build.
        if options.resource_file:
            update_last_usage = False
        else:
            update_last_usage = True

        storage = sandbox_storage.SandboxStorage(
            build_root,
            custom_fetcher=options.custom_fetcher,
            oauth_token=token,
            update_last_usage=update_last_usage,
            use_cached_only=not app_config.have_sandbox_fetcher,
        )
        logger.debug("Getting resource %s", resource_id)
        resource = storage.get(
            resource_id, decompress_if_archive=False, rename_to=options.rename_to, resource_file=options.resource_file
        )
        if options.dir_output_tared_path is not None:
            storage.dir_outputs_prepare_downloaded_resource(
                resource, resource_id, dir_outputs_in_runner=not options.for_dist_build
            )
        logger.debug("downloaded resource info (compacted): %s", compact_resinfo(resource))
    except Exception as e:
        logger.exception("Error while downloading resource %s", resource_id)
        if devtools.ya.core.error.is_temporary_error(e):
            logger.exception("Error is considered to be temporary - exit with INFRASTRUCTURE_ERROR")
            return devtools.ya.core.error.ExitCodes.INFRASTRUCTURE_ERROR
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
