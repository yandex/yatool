import hashlib
import json
import json.decoder as json_decoder
import logging
import math
import os
import os.path as os_path
import threading
import stat
import tempfile
import re
import shutil

from exts import hashing
from six.moves.urllib import parse
from yalibrary.store.dist_store import DistStore
import yalibrary.store.bazel_store.protocol as _protocol
from library.python import func
import library.python.retry as retry
import requests
import requests.adapters
import requests.auth
import urllib3.exceptions
import zstandard as zstd

# Сохраняем публичные имена после выделения протокола в отдельный модуль.
META_VERSION = _protocol.META_VERSION
SHA256_LENGTH = _protocol.SHA256_LENGTH
uid_to_uri = _protocol.uid_to_uri

DOWNLOAD_CHUNK_SIZE = 1 << 15
DEFAULT_TIMEOUT = 30

EXCLUDED_P = frozenset(['UN', 'PK', 'GO', 'ld', 'SB', 'CP', 'DL'])

logger = logging.getLogger(__name__)


def always_false(x, y):
    return False


class BazelStoreException(Exception):
    """
    Retryable errors related to store
    """

    pass


class BazelStoreHTTPError(BazelStoreException):
    def __init__(self, status_code):
        super().__init__("Bazel cache returned HTTP {}".format(status_code))
        self.retriable = status_code in (408, 429) or status_code >= 500


class BazelStoreLocalError(BazelStoreException):
    retriable = False


class BazelStoreIntegrityError(BazelStoreException):
    retriable = False


class BazelStoreBrokenException(BazelStoreException):
    """
    Permanently disables dist store
    """

    pass


class BazelStoreRollbackError(RuntimeError):
    """The build directory is unsafe to reuse after a failed rollback."""


class TimeoutHTTPAdapter(requests.adapters.HTTPAdapter):
    def __init__(self, *args, timeout=DEFAULT_TIMEOUT, **kwargs):
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("Bazel HTTP timeout must be a finite positive number of seconds")
        self._timeout = timeout
        super().__init__(*args, **kwargs)

    def send(
        self,
        request,
        stream,
        timeout=None,
        verify=True,
        cert=None,
        proxies=None,
    ):
        if timeout is None:
            timeout = self._timeout
        return super().send(request=request, stream=stream, timeout=timeout, verify=verify, cert=cert, proxies=proxies)


class _RetryPolicy:
    _BACKOFF = 4
    _DELAY_SECS = 0.25
    _READ_RETRIES_COUNT = 3  # 0.25, 1, 4
    _WRITE_RETRIES_COUNT = 4  # 0.25, 1, 4, 16

    def _build_conf(self, retries):
        def retry_handler(error, n, raised_after):
            logger.debug("error: {}, iteration num: {}, raised after: {}".format(error, n, raised_after))

        return (
            retry.RetryConf(
                retriable=lambda e: getattr(e, 'retriable', True) is True,
                handle_error=retry_handler,
            )
            .waiting(delay=self._DELAY_SECS, backoff=self._BACKOFF)
            .upto_retries(retries)
        )

    @func.lazy_property
    def read_conf(self):
        return self._build_conf(self._READ_RETRIES_COUNT)

    @func.lazy_property
    def write_conf(self):
        return self._build_conf(self._WRITE_RETRIES_COUNT)


DEFAULT_RETRY_POLICY = _RetryPolicy()


class StreamWrapper:
    def __init__(self, file_name):
        self._fo = open(file_name, 'wb')
        self._hasher = hashlib.sha256()

    def write(self, data):
        self._fo.write(data)
        self._hasher.update(data)

    def close(self):
        self._fo.close()

    def get_hexdigest(self):
        return self._hasher.hexdigest()


class StreamWriter:
    def __init__(self, file_name, codec=None):
        self.file_name = file_name
        self._codec = codec
        self._inner_writer = None
        self._output_writer = None

    def __enter__(self):
        self._inner_writer = StreamWrapper(self.file_name)
        if self._codec is None:
            self._output_writer = self._inner_writer
        elif self._codec == "zstd":
            dctx = zstd.ZstdDecompressor()
            self._output_writer = dctx.stream_writer(self._inner_writer)
        else:
            raise ValueError("Unknown codec: {}".format(self._codec))
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._output_writer.close()

    def write(self, data):
        self._output_writer.write(data)

    def get_hexdigest(self):
        return self._inner_writer.get_hexdigest()


class BazelStoreClient(object):
    def __init__(
        self, base_uri, username=None, password=None, max_connections=48, retry_policy=None, timeout=DEFAULT_TIMEOUT
    ):
        self.base_uri = base_uri.rstrip('/') + '/'
        self.session = requests.Session()
        adapter = TimeoutHTTPAdapter(pool_connections=max_connections, pool_maxsize=max_connections, timeout=timeout)
        self.session.mount('http://', adapter)
        self.session.mount('https://', adapter)
        if username and password:
            self.session.auth = requests.auth.HTTPBasicAuth(username, password)
        self.retry_policy = retry_policy or DEFAULT_RETRY_POLICY

    def _retry_func(self, func, f_args=(), f_kwargs=None, conf=retry.DEFAULT_CONF):
        if f_kwargs is None:
            f_kwargs = {}

        def call():
            try:
                return func(*f_args, **f_kwargs)
            except requests.ConnectTimeout:
                # Keep the type until retries finish: a TCP black hole must not stall every cache operation.
                raise
            except requests.RequestException as e:
                # Other request failures remain cache misses after their retries.
                raise BazelStoreException(str(e)) from e
            except OSError as e:
                raise BazelStoreLocalError("Local filesystem error: {}".format(e)) from e

        try:
            return retry.retry_call(call, conf=conf)
        except requests.ConnectTimeout as e:
            raise BazelStoreBrokenException("Bazel cache connection timed out after retries: {}".format(e)) from e
        except BazelStoreException:
            raise
        except Exception as e:
            raise BazelStoreBrokenException(e)

    def _ac_url(self, uid):
        return parse.urljoin(self.base_uri, 'ac/' + uid_to_uri(uid))

    def _cas_url(self, hex_digest):
        return parse.urljoin(self.base_uri, 'cas/' + hex_digest)

    @staticmethod
    def _check_response(response):
        if not 200 <= response.status_code < 300:
            raise BazelStoreHTTPError(response.status_code)

    def install_data(self, file_path, cas_url, codec, headers):
        with self.session.get(cas_url, headers=headers, stream=True) as req:
            if req.status_code == 404:
                raise BazelStoreIntegrityError("Requested blob is missing in CAS: {}".format(cas_url))

            self._check_response(req)

            content_encoding = req.headers.get('content-encoding', 'identity').lower()
            if content_encoding not in ('identity', 'zstd'):
                raise BazelStoreIntegrityError("Unsupported CAS content encoding: '{}'".format(content_encoding))

            try:
                with StreamWriter(file_path, codec='zstd' if content_encoding == 'zstd' else None) as writer:
                    # urllib3 otherwise accepts early EOF when the server supplied Content-Length.
                    req.raw.enforce_content_length = True
                    # Requests may decode Content-Encoding in iter_content(). Decode zstd exactly once here.
                    for chunk in req.raw.stream(DOWNLOAD_CHUNK_SIZE, decode_content=False):
                        writer.write(chunk)
                return writer.get_hexdigest()
            except zstd.ZstdError as e:
                raise BazelStoreIntegrityError("Invalid zstd data in CAS") from e
            except urllib3.exceptions.HTTPError as e:
                raise requests.ConnectionError(str(e)) from e

    def get_codec(self):
        return "zstd"

    def get_blob(self, hash, file_path, expected_size=None):
        cas_url = self._cas_url(hash)
        dirname = os_path.dirname(os_path.abspath(file_path))
        os.makedirs(dirname, exist_ok=True)
        codec = self.get_codec()
        headers = {'Accept-Encoding': codec}
        fd, temporary_path = tempfile.mkstemp(prefix='.bazel-', dir=dirname)
        os.close(fd)
        try:
            digest_got = self._retry_func(
                self.install_data, f_args=(temporary_path, cas_url, codec, headers), conf=self.retry_policy.read_conf
            )
            if digest_got != hash:
                raise BazelStoreIntegrityError("CAS digest mismatch: got {}, expected {}".format(digest_got, hash))
            if expected_size is not None and os.stat(temporary_path).st_size != expected_size:
                raise BazelStoreIntegrityError("CAS size does not match metadata")
            os.replace(temporary_path, file_path)
        finally:
            if os_path.exists(temporary_path):
                os.unlink(temporary_path)

    def put_blob(self, file_path):
        hashstr = hashing.file_hash(file_path, hashlib.sha256())
        size = os.stat(file_path).st_size

        cas_url = self._cas_url(hashstr)
        if size == 0:
            with self.session.put(cas_url, b"", allow_redirects=False) as response:
                self._check_response(response)
        else:
            with open(file_path, 'rb') as afile:
                with self.session.put(cas_url, afile, allow_redirects=False) as response:
                    self._check_response(response)
        return hashstr

    def put_meta(self, uid, meta):
        ac_url = self._ac_url(uid)
        # A redirect can turn PUT into GET and falsely report a successful upload.
        with self.session.put(ac_url, json.dumps(meta), allow_redirects=False) as response:
            self._check_response(response)

    def _get_meta(self, uid):
        with self.session.get(self._ac_url(uid)) as response:
            if response.status_code == 404:
                raise BazelStoreIntegrityError("No metadata for UID '{}'".format(uid))
            self._check_response(response)
            try:
                return json.loads(response.content)
            except (json_decoder.JSONDecodeError, UnicodeDecodeError) as e:
                raise BazelStoreIntegrityError("Broken metadata for UID '{}'".format(uid)) from e

    def get_meta(self, uid):
        return self._retry_func(self._get_meta, f_args=(uid,), conf=self.retry_policy.read_conf)

    def put_data(self, files, root_dir, uid, name):
        result = {'files': {}, 'name': name}
        for file in sorted(files):
            if not file.startswith(root_dir):
                raise AssertionError('File is outside of rootpath')

            stat = os.stat(file)
            result['files'][os_path.relpath(file, root_dir)] = {
                'hash': self._retry_func(
                    self.put_blob, f_args=(os_path.abspath(file),), conf=self.retry_policy.write_conf
                ),
                'executable': os.access(file, os.X_OK),
                'mode': stat.st_mode,
                'size': stat.st_size,
            }
        self._retry_func(self.put_meta, f_args=(uid, result), conf=self.retry_policy.write_conf)
        return result

    def download_file(self, file_path, file_data):
        self.get_blob(file_data['hash'], file_path, expected_size=file_data.get('size'))
        os.chmod(file_path, file_data['mode'])

    @staticmethod
    def _validate_meta(meta, root_dir):
        if not isinstance(meta, dict) or not isinstance(meta.get('files'), dict):
            raise BazelStoreIntegrityError("Metadata must contain a mapping of files")
        root = os_path.realpath(root_dir)
        paths = set()
        for rel_path, data in meta['files'].items():
            if (
                not isinstance(rel_path, str)
                or not rel_path
                or os_path.isabs(rel_path)
                or '..' in rel_path.replace(os.altsep or os.sep, os.sep).split(os.sep)
            ):
                raise BazelStoreIntegrityError("Metadata contains an invalid relative path")
            path = os_path.realpath(os_path.join(root, rel_path))
            if os_path.commonpath((root, path)) != root or path == root:
                raise BazelStoreIntegrityError("Metadata file is outside the build directory")
            if path in paths:
                raise BazelStoreIntegrityError("Metadata contains duplicate file paths")
            paths.add(path)
            if not isinstance(data, dict) or not isinstance(data.get('hash'), str):
                raise BazelStoreIntegrityError("Metadata contains an invalid file description")
            if re.fullmatch(r'[0-9a-f]{64}', data['hash']) is None:
                raise BazelStoreIntegrityError("Metadata contains an invalid CAS digest")
            if type(data.get('mode')) is not int or not 0 <= data['mode'] <= 0o177777:
                raise BazelStoreIntegrityError("Metadata contains an invalid file mode")
            # Older entries may omit sizes. Verify a declared size before installing the file.
            if 'size' in data and (type(data['size']) is not int or data['size'] < 0):
                raise BazelStoreIntegrityError("Metadata contains an invalid file size")

        for path in paths:
            parent = os_path.dirname(path)
            while parent != root:
                if parent in paths:
                    raise BazelStoreIntegrityError("Metadata uses a file as a directory")
                parent = os_path.dirname(parent)

    @staticmethod
    def _install_staged_files(staging_dir, root_dir, backup_dir):
        installed = []
        created_dirs = []
        root = os_path.realpath(root_dir)

        def on_walk_error(error):
            raise error

        try:
            for dirname, _, filenames in os.walk(staging_dir, onerror=on_walk_error):
                for filename in filenames:
                    source = os_path.join(dirname, filename)
                    relative = os_path.relpath(source, staging_dir)
                    destination = os_path.join(root_dir, relative)
                    if os_path.commonpath((root, os_path.realpath(destination))) != root:
                        raise BazelStoreIntegrityError("Filtered file is outside the build directory")
                    parent = os_path.dirname(destination)
                    missing_dirs = []
                    while not os_path.exists(parent):
                        missing_dirs.append(parent)
                        parent = os_path.dirname(parent)
                    created_dirs.extend(reversed(missing_dirs))
                    os.makedirs(os_path.dirname(destination), exist_ok=True)
                    if os_path.isdir(destination):
                        raise IsADirectoryError(destination)
                    backup = None
                    if os_path.lexists(destination):
                        backup = os_path.join(backup_dir, str(len(installed)))
                        os.replace(destination, backup)
                    installed.append((destination, backup))
                    os.replace(source, destination)
        except Exception:
            # Restore the previous files if installing a later file fails.
            rollback_error = None
            for destination, backup in reversed(installed):
                try:
                    if backup is not None:
                        os.replace(backup, destination)
                    elif os_path.lexists(destination):
                        os.unlink(destination)
                except OSError as e:
                    rollback_error = e
            for directory in reversed(created_dirs):
                try:
                    if os_path.exists(directory):
                        os.rmdir(directory)
                except OSError as e:
                    rollback_error = e
            if rollback_error is not None:
                raise BazelStoreRollbackError(
                    "Failed to restore the build directory; backups kept in " + backup_dir
                ) from rollback_error
            raise

    def get_data(self, root_dir, uid, filter_func=None, expected_outputs=None):
        meta = self.get_meta(uid)
        try:
            self._validate_meta(meta, root_dir)
        except (ValueError, OSError) as e:
            raise BazelStoreIntegrityError("Metadata contains an invalid file path") from e
        if filter_func is None:
            filter_func = always_false

        try:
            os.makedirs(root_dir, exist_ok=True)
            temporary_dir = tempfile.mkdtemp(prefix='.bazel-', dir=os_path.dirname(os_path.abspath(root_dir)))
            cleanup = True
            try:
                staging_dir = os_path.join(temporary_dir, 'files')
                backup_dir = os_path.join(temporary_dir, 'backups')
                os.makedirs(staging_dir)
                os.makedirs(backup_dir)
                # Keep both downloaded files and links created by the filter out of the build root until ready.
                for rel_path, file_data in meta['files'].items():
                    file_path = os_path.join(staging_dir, rel_path)
                    if not filter_func(file_path, self._cas_url(file_data['hash'])):
                        self.download_file(file_path, file_data)
                # An existing dependency file must not hide an incomplete cached result.
                for output in expected_outputs or ():
                    relative = os_path.relpath(output, root_dir)
                    try:
                        mode = os.lstat(os_path.join(staging_dir, relative)).st_mode
                    except FileNotFoundError as e:
                        raise BazelStoreIntegrityError(
                            "Required output is missing from cached result: " + relative
                        ) from e
                    if not stat.S_ISREG(mode):
                        raise BazelStoreIntegrityError("Required output is not a regular file: " + relative)
                try:
                    self._install_staged_files(staging_dir, root_dir, backup_dir)
                except BazelStoreRollbackError:
                    # A filesystem failure during rollback must not delete the saved original files.
                    cleanup = False
                    raise
            finally:
                if cleanup:
                    try:
                        shutil.rmtree(temporary_dir)
                    except OSError:
                        # Cleanup cannot turn an installed result into a cache miss.
                        logger.warning("Failed to remove temporary cache directory %s", temporary_dir, exc_info=True)
        except OSError as e:
            raise BazelStoreException("Failed to install files from Bazel cache: {}".format(e)) from e
        return meta

    def _exists(self, uid):
        with self.session.head(self._ac_url(uid)) as response:
            if response.status_code == 404:
                return False
            self._check_response(response)
            return response.status_code == 200

    def exists(self, uid):
        return self._retry_func(self._exists, f_args=(uid,), conf=self.retry_policy.read_conf)


class BazelStore(DistStore):
    # Records use graph UIDs; put() does not write content UID aliases.
    support_content_uids = False

    def __init__(self, *args, **kwargs):
        self._put_observer = kwargs.pop('put_observer', None)
        super(BazelStore, self).__init__(
            name='bazel-store',
            stats_name='bazel_store_status',
            tag='BAZEL',
            readonly=kwargs.pop('readonly', True),
            fits_filter=kwargs.pop('fits_filter', None),
            max_file_size=kwargs.pop('max_file_size', 0),
        )
        self._client = BazelStoreClient(*args, **kwargs)
        self._disabled = False
        self._failed_restores = set()
        self._lock = threading.Lock()

    def put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        """Наблюдатель получает статус и живые выходы; без него обычная запись не меняется."""
        status = super().put(self_uid, uid, root_dir, files, codec=codec, cuid=cuid)
        if self._put_observer is not None:
            self._put_observer(uid, status.name.lower(), root_dir, files)
        return status

    @property
    def base_uri(self):
        return self._client.base_uri

    @property
    def disabled(self):
        return self._disabled

    def _get_data_size(self, meta_info):
        return sum(x.get('size', 0) for x in meta_info['files'].values())

    def _inc_cache_hit(self, found):
        self._cache_hit['requested'] += 1
        if found:
            self._cache_hit['found'] += 1

    def _disable_store(self, reason):
        if not self._disabled:
            with self._lock:
                if not self._disabled:
                    logger.error("Disabling bazel store due error: %s", reason)
                    self._disabled = True

    def fits(self, node):
        if isinstance(node, dict):
            outputs = node["outputs"]
            kv = node.get("kv") or {}
        else:
            outputs = node.outputs
            kv = node.kv or {}

        if not len(outputs):
            return False

        if self._fits_filter and not self._fits_filter(node):
            return False

        p = kv.get('p')
        if p in EXCLUDED_P:
            return False

        for o in outputs:
            for p in 'library/cpp/svnversion', 'library/cpp/build_info':
                if o.startswith('$(BUILD_ROOT)/' + p):
                    return False
        if all(o.endswith('.tar') for o in outputs):
            return False
        return True

    def _do_has(self, uid):
        if self._disabled:
            return False
        with self._lock:
            if uid in self._failed_restores:
                return False

        try:
            found = self._client.exists(uid)
        except BazelStoreBrokenException as e:
            self._disable_store(e)
            self._count_failure('has')
            found = False
        except BazelStoreException as e:
            logger.debug('Failed to probe %s: %s', uid, e)
            self._count_failure('has')
            found = False
        logger.debug('Bazel-remote Probing %s => %s', uid, found)
        self._inc_cache_hit(found)
        return found

    def _do_put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        if self._disabled:
            return False

        name = files[0][len(root_dir) + 1 :] if len(files) else 'none'
        logger.debug('Put %s(%s) to Bazel-remote', name, uid)
        if uid in self._meta or self.readonly():
            # should never happen
            logger.debug('Put %s(%s) to Bazel-remote completed(no-op)', name, uid)
            return True
        try:
            meta = self._client.put_data(files, root_dir, uid, name)
            with self._lock:
                self._meta[uid] = meta
                self._failed_restores.discard(uid)
        except BazelStoreBrokenException as e:
            self._disable_store(e)
            self._count_failure('put')
            return False
        except BazelStoreException as e:
            logger.debug('Put %s(%s) to Bazel-remote failed: %s', name, uid, e)
            self._count_failure('put')
            return False

        data_size = self._get_data_size(meta)
        logger.debug('Put %s(%s) size=%d to Bazel-remote completed', name, uid, data_size)
        self._inc_data_size(data_size, 'put')
        return True

    def _do_try_restore(self, uid, into_dir, filter_func=None, expected_outputs=None):
        if self._disabled:
            return False

        try:
            meta = self._client.get_data(into_dir, uid, filter_func, expected_outputs=expected_outputs)
        except BazelStoreBrokenException as e:
            self._disable_store(e)
            self._count_failure('get')
            return False
        except BazelStoreException as e:
            logger.debug('Failed to restore %s: %s', uid, e)
            # AC may still exist while a referenced CAS file is missing. Permit repair after rebuilding.
            with self._lock:
                self._failed_restores.add(uid)
                self._meta.pop(uid, None)
            self._count_failure('get')
            return False

        with self._lock:
            self._failed_restores.discard(uid)
        data_size = self._get_data_size(meta)
        self._inc_data_size(data_size, 'get')
        return True

    @property
    def avg_compression_ratio(self):
        return 1.0


class MultiBazelStore(DistStore):
    """Ordered chain of Bazel caches: reads fall through the chain, writes go to the first cache only."""

    support_content_uids = False

    def __init__(self, stores):
        if not stores:
            raise ValueError('MultiBazelStore requires at least one BazelStore')
        self._stores = list(stores)
        self._primary = self._stores[0]
        super(MultiBazelStore, self).__init__(
            name='bazel-store',
            stats_name='bazel_store_status',
            tag='BAZEL',
            readonly=self._primary.readonly(),
        )

    @property
    def stores(self):
        return list(self._stores)

    @property
    def primary(self):
        return self._primary

    @property
    def disabled(self):
        return all(store.disabled for store in self._stores)

    def fits(self, node):
        return self._primary.fits(node)

    def prepare(self, *args, **kwargs):
        return

    def readonly(self):
        return self._primary.readonly()

    # Public methods are overridden on purpose: every child keeps its own timers and counters,
    # the chain itself only aggregates them in stats().
    def has(self, uid):
        for store in self._stores:
            if store.disabled:
                continue
            if store.has(uid):
                return True
        return False

    def try_restore(self, uid, into_dir, filter_func=None, expected_outputs=None):
        for store in self._stores:
            if store.disabled:
                continue
            if store.try_restore(uid, into_dir, filter_func=filter_func, expected_outputs=expected_outputs):
                return True
        return False

    def put(self, self_uid, uid, root_dir, files, codec=None, cuid=None):
        return self._primary.put(self_uid, uid, root_dir, files, codec=codec, cuid=cuid)

    @property
    def avg_compression_ratio(self):
        return 1.0

    def _cache_summary(self, store):
        return {
            'base_uri': store.base_uri,
            'cache_hit': dict(store._cache_hit),
            'failures': dict(store._failures),
            'disabled': store.disabled,
        }

    def _aggregate(self):
        for key in self._timers:
            self._timers[key] = sum(store._timers.get(key, 0) for store in self._stores)
        for key in self._time_intervals:
            intervals = []
            for store in self._stores:
                intervals.extend(store._time_intervals.get(key, []))
            self._time_intervals[key] = intervals
        for key in self._counters:
            self._counters[key] = sum(store._counters.get(key, 0) for store in self._stores)
        for key in self._failures:
            self._failures[key] = sum(store._failures.get(key, 0) for store in self._stores)
        for key in self._data_size:
            self._data_size[key] = sum(store._data_size.get(key, 0) for store in self._stores)
        for key in self._cache_hit:
            self._cache_hit[key] = sum(store._cache_hit.get(key, 0) for store in self._stores)

    def stats(self, execution_log, evlog_writer):
        self._aggregate()
        summaries = [self._cache_summary(store) for store in self._stores]
        for summary in summaries:
            logger.debug(
                'Bazel cache %s: requested=%d found=%d failures=%s disabled=%s',
                summary['base_uri'],
                summary['cache_hit']['requested'],
                summary['cache_hit']['found'],
                summary['failures'],
                summary['disabled'],
            )

        super(MultiBazelStore, self).stats(execution_log, self._wrap_evlog_writer(evlog_writer, summaries))

    @staticmethod
    def _wrap_evlog_writer(evlog_writer, summaries):
        if evlog_writer is None:
            return None

        def writer(name, **kwargs):
            kwargs['caches'] = summaries
            return evlog_writer(name, **kwargs)

        return writer
