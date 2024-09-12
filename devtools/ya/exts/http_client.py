import base64
import hashlib
import logging
import os
import six
import socket
import stat
import time

from six.moves import urllib

import exts.io2
import exts.fs
import exts.hashing
import exts.retry
import exts.process
import library.python.func
import library.python.windows

import typing as tp  # noqa

logger = logging.getLogger(__name__)

hasher_map = {
    'md5': hashlib.md5,
    'sha1': hashlib.sha1,
    'sha512': hashlib.sha512,
}

integrity_encodings = {
    'base64': lambda x: base64.b64encode(x.digest()).decode('utf-8'),
    'hex': lambda x: six.ensure_str(x.hexdigest()),
}


class BadIntegrityAlgorithmException(Exception):
    def __init__(self, alg):
        # type: (str) -> None
        msg = 'Integrity algorithm supports {0}, got {1}'.format(hasher_map.keys(), alg)
        super(BadIntegrityAlgorithmException, self).__init__(msg)


class BadIntegrityEncodingException(Exception):
    def __init__(self, mode):
        # type: (str) -> None
        msg = 'Integrity encoding supports {0}, got {1}'.format(integrity_encodings.keys(), mode)
        super(BadIntegrityEncodingException, self).__init__(msg)


class BadMD5Exception(Exception):
    temporary = True


class DownloadTimeoutException(Exception):
    mute = True
    temporary = True


def make_user_agent():
    return 'ya: {host}'.format(host=socket.gethostname())


def make_headers(headers=None):
    # type: (dict[str, str] | None) -> dict
    result = {'User-Agent': make_user_agent()}
    if headers is None:
        return result
    result.update(headers)
    return result


def download_file(url, path, mode=0, expected_md5=None, headers=None):
    # type: (str, str, int, str | None, dict[str, str] | None) -> None
    # This value emulates old logic when expected integrity is not provided
    # but md5 sum has to be calculated
    integrity = 'md5-'
    if expected_md5:
        integrity += expected_md5

    download_file_with_integrity(url, path, integrity, 'hex', mode, headers)


@exts.retry.retrying(max_times=7, retry_sleep=lambda i, t: i * 5)
def download_file_with_integrity(url, path, integrity, integrity_encoding='base64', mode=0, headers=None):
    # type: (str, str, str, str, int, dict[str, str] | None) -> None
    alg, expected_integrity = integrity.split("-")

    if alg not in hasher_map:
        raise BadIntegrityAlgorithmException(alg)

    if integrity_encoding not in integrity_encoding:
        raise BadIntegrityEncodingException(integrity_encoding)

    exts.fs.ensure_removed(path)
    exts.fs.create_dirs(os.path.dirname(path))

    checksum = hasher_map.get(alg)()
    chunks_sizes = []

    logger.debug('Downloading %s to %s, expect %s', url, path, integrity)
    start_time = time.time()
    try:
        request = urllib.request.Request(url)
        for k, v in six.iteritems(make_headers(headers=headers)):
            request.add_header(k, v)
        if library.python.windows.on_win():
            # windows firewall hack
            timeout = socket._GLOBAL_DEFAULT_TIMEOUT
        else:
            timeout = 30
        res = urllib.request.urlopen(request, timeout=timeout)
    except urllib.error.URLError as e:
        if isinstance(e.reason, socket.timeout):
            raise DownloadTimeoutException(e)
        else:
            raise e
    except socket.timeout as e:
        raise DownloadTimeoutException(e)

    logger.debug('Request to %s has headers %s', url, res.info())

    with open(path, 'wb') as dest_file:
        exts.io2.copy_stream(res.read, dest_file.write, checksum.update, lambda d: chunks_sizes.append(len(d)))

    checksum_str = integrity_encodings.get(integrity_encoding)(checksum)

    if expected_integrity and expected_integrity != checksum_str:
        raise BadMD5Exception('{} sum expected {}, but was {}'.format(alg, expected_integrity, checksum_str))

    os.chmod(path, stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IROTH | mode)

    logger.debug(
        'Downloading finished %s to %s, %s=%s, size=%s, elapsed=%f',
        url,
        path,
        alg,
        checksum_str,
        str(sum(chunks_sizes)),
        time.time() - start_time,
    )


def _http_call(url, method, data=None, headers=None, timeout=30):
    # type: (str, str, tp.Any, dict[str, str] | None, int) -> bytes
    logger.debug('%s request using urllib2 %s%s', method, url, ', {} bytes'.format(len(data)) if data else '')
    start_time = time.time()
    req = urllib.request.Request(url, data, headers=make_headers(headers))
    req.get_method = lambda: method
    res = urllib.request.urlopen(req, timeout=timeout).read()
    logger.debug(
        'Finished %s request using urllib2 %s%s, elapsed=%f',
        method,
        url,
        ', {} bytes'.format(len(data)) if data else '',
        time.time() - start_time,
    )
    return res


def http_patch(url, data, headers=None, timeout=30):
    return _http_call(url, 'PATCH', data, headers, timeout)


def http_post(url, data, headers=None, timeout=30):
    return _http_call(url, 'POST', data, headers, timeout)


def http_put(url, data, headers=None, timeout=30):
    return _http_call(url, 'PUT', data, headers, timeout)


def http_delete(url, headers=None, timeout=30):
    return _http_call(url, 'DELETE', None, headers, timeout)


@exts.retry.retrying(
    max_times=3,
    retry_sleep=lambda i, t: i * 5,
    raise_exception=lambda e: isinstance(e, urllib.error.HTTPError) and e.code == 404,
)
def http_get(url, headers=None, data=None, timeout=30):
    return _http_call(url, 'GET', data, headers, timeout=timeout)
