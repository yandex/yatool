import os
import stat
import logging
import collections

import exts
import exts.http_client
import exts.uniq_id

try:
    import devtools.libs.universal_fetcher.py as universal_fetcher
except ImportError:
    universal_fetcher = None

logger = logging.getLogger(__name__)

FetchResponse = collections.namedtuple(
    "FetchResponse",
    (
        "result",
        "download_time_ms",
        "size",
    ),
)


@exts.func.memoize(thread_safe=False)
def _get_ufetcher():
    if universal_fetcher is None:
        return None

    retry_policy = universal_fetcher.RetryPolicy(
        max_retry_count=7,
        initial_delay_ms=5000,
        use_fixed_delay=False,
        max_delay_ms=60000,
        backoff_multiplier=1.5,
    )

    http_params = universal_fetcher.HttpParams(
        user_agent=exts.http_client.make_user_agent(),
        socket_timeout_ms=30000,
        connect_timeout_ms=30000,
    )

    http_config = universal_fetcher.HttpConfig(http_params, retry_policy)

    cfg = universal_fetcher.FetchersConfig(http_config)

    json_conf = cfg.build()

    return universal_fetcher.UniversalFetcher(json_conf, logger)


def _download_file_by_ufetcher(url, path, additional_file_perms=0, expected_md5=None, headers=None, raise_err=False):
    temp_path = "{}.{}.part".format(path, exts.uniq_id.gen8())

    ufetcher = _get_ufetcher()

    exts.fs.ensure_removed(path)
    exts.fs.ensure_removed(temp_path)
    exts.fs.create_dirs(os.path.dirname(path))

    dst_path = os.path.dirname(temp_path)
    filename = os.path.basename(temp_path)

    result = ufetcher.download(url, dst_path, filename)
    error = result['last_attempt']['result']['error']

    if error:
        if raise_err:
            raise RuntimeError(error)
        return result

    os.rename(temp_path, path)
    os.chmod(path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH | additional_file_perms)

    return result


def download_file(
    url, path, additional_file_perms=0, expected_md5=None, headers=None, use_universal_fetcher=False, raise_err=False
):
    if use_universal_fetcher and universal_fetcher is not None:
        result = _download_file_by_ufetcher(url, path, additional_file_perms, expected_md5, headers, raise_err)
        last_attempt = result.get("last_attempt", {})
        size = last_attempt.get("result", {}).get("resource_info", {}).get("size", 0)
        download_time_ms = last_attempt.get("duration_us", 0) / 1000
        logger.debug("Downloaded file %s to %s in %d ms", url, path, download_time_ms)
        return FetchResponse(result, download_time_ms, size)

    download_time_ms, size = exts.http_client.download_file(url, path, additional_file_perms, expected_md5, headers)
    return FetchResponse(None, download_time_ms, size)
