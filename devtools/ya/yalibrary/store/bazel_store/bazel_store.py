import binascii
import hashlib
import json
import json.decoder as json_decoder
import logging
import os
import os.path as os_path

import requests
import requests.adapters
import requests.auth
from six.moves.urllib import parse

from exts.func import memoize
from exts import hashing
from yalibrary.store.dist_store import DistStore
import zstandard as zstd

DOWNLOAD_CHUNK_SIZE = 1 << 15
META_VERSION = '1'
SHA256_LENGTH = 64

EXCLUDED_P = frozenset(['UN', 'PK', 'GO', 'ld', 'SB', 'CP', 'DL'])

logger = logging.getLogger(__name__)


def always_false(x, y):
    return False


class BazelStoreException(Exception):
    pass


# Byte interpretation:
# - second last: hash type (0-no hash, 1-sha256)
# - last: META_VERSION
def uid_to_uri(uid):
    pseudo_hash = binascii.hexlify(uid.encode('utf-8')).decode('utf-8')
    padding_length = SHA256_LENGTH - len(pseudo_hash)
    if padding_length < 2:
        pseudo_hash = hashlib.sha256(pseudo_hash.encode('utf-8')).hexdigest()[:-2]
        pseudo_hash += "1" + META_VERSION
    else:
        pseudo_hash += '0' * (padding_length - 1) + META_VERSION
    return pseudo_hash


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
    def __init__(self, base_uri, username=None, password=None, max_connections=48, client_decompress=False):
        self.base_uri = base_uri
        self.session = requests.Session()
        adapter = requests.adapters.HTTPAdapter(pool_connections=max_connections, pool_maxsize=max_connections)
        self.session.mount('http://', adapter)
        self.session.mount('https://', adapter)
        if username and password:
            self.session.auth = requests.auth.HTTPBasicAuth(username, password)
        self.client_decompress = client_decompress

    def _ac_url(self, uid):
        return parse.urljoin(self.base_uri, '/ac/' + uid_to_uri(uid))

    def _cas_url(self, hex_digest):
        return parse.urljoin(self.base_uri, '/cas/' + hex_digest)

    def get_codec(self):
        if self.client_decompress:
            return "zstd"
        else:
            return None

    def get_blob(self, hash, file_path):
        cas_url = self._cas_url(hash)
        dirname = os_path.dirname(file_path)
        if not os_path.exists(dirname):
            os.makedirs(dirname)
        headers = None
        codec = self.get_codec()
        if codec == "zstd":
            headers = {'Accept-Encoding': 'zstd'}
        with StreamWriter(file_path, codec=codec) as writer, self.session.get(cas_url, headers=headers) as get:
            for chunk in get.iter_content(chunk_size=DOWNLOAD_CHUNK_SIZE):
                writer.write(chunk)
            digest_got = writer.get_hexdigest()
        if digest_got != hash:
            raise BazelStoreException(
                'Digest mismatch got: %s expected: %s',
                digest_got,
                hash,
            )

    def put_blob(self, file_path):
        hashstr = hashing.file_hash(file_path, hashlib.sha256())
        size = os.stat(file_path).st_size

        cas_url = self._cas_url(hashstr)
        if size == 0:
            response = self.session.put(cas_url, b"")
        else:
            with open(file_path, 'rb') as afile:
                response = self.session.put(cas_url, afile)

        if response.status_code != 200:
            raise BazelStoreException(
                'Failed to upload file %s, status_code %d',
                file_path,
                response.status_code,
            )
        return hashstr

    def put_meta(self, uid, meta):
        ac_url = self._ac_url(uid)
        self.session.put(ac_url, json.dumps(meta))

    def get_meta(self, uid):
        ac_url = self._ac_url(uid)
        response = self.session.get(ac_url)
        if response.status_code != 200:
            raise BazelStoreException('No metadata for UID `%s`', uid)
        try:
            result = json.loads(response.content)
        except json_decoder.JSONDecodeError:
            raise BazelStoreException('Broken metadata for UID `%s`', uid)
        return result

    def put_data(self, files, root_dir, uid, name):
        result = {'files': {}, 'name': name}
        for file in sorted(files):
            if not file.startswith(root_dir):
                raise BazelStoreException('File is outside of rootpath')

            stat = os.stat(file)
            result['files'][os_path.relpath(file, root_dir)] = {
                'hash': self.put_blob(os_path.abspath(file)),
                'executable': os.access(file, os.X_OK),
                'mode': stat.st_mode,
                'size': stat.st_size,
            }

        self.put_meta(uid, result)
        return result

    def download_file(self, file_path, file_data):
        self.get_blob(file_data['hash'], file_path)
        os.chmod(file_path, file_data['mode'])

    def get_data(self, root_dir, uid, filter_func=None):
        meta = self.get_meta(uid)
        if filter_func is None:
            filter_func = always_false

        for rel_path, file_data in meta['files'].items():
            file_path = os_path.join(root_dir, rel_path)
            if not filter_func(file_path, self._cas_url(file_data['hash'])):
                self.download_file(file_path, file_data)
        return meta

    def exists(self, uid):
        ac_url = self._ac_url(uid)
        response = self.session.head(ac_url)
        if response.status_code == 200:
            return True
        return False


class BazelStore(DistStore):
    def __init__(self, *args, **kwargs):
        super(BazelStore, self).__init__(
            name='bazel-store',
            stats_name='bazel_store_status',
            tag='BAZEL',
            readonly=kwargs.pop('readonly'),
            fits_filter=kwargs.pop('fits_filter', None),
        )
        self._client = BazelStoreClient(*args, **kwargs)

    def _get_data_size(self, meta_info):
        return sum(x.get('size', 0) for x in meta_info['files'].values())

    def _inc_cache_hit(self, found):
        self._cache_hit['requested'] += 1
        if found:
            self._cache_hit['found'] += 1

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

    def load_meta(self, uids, heater_mode=False, refresh_on_read=False):
        # Meta preloading is no required for bazel store
        return

    @memoize(thread_safe=False)
    def _do_has(self, uid):
        found = self._client.exists(uid)

        logger.debug('Bazel-remote Probing %s => %s', uid, found)
        self._inc_cache_hit(found)
        return found

    def _do_put(self, uid, root_dir, files, codec=None):
        name = files[0][len(root_dir) + 1 :] if len(files) else 'none'
        logger.debug('Put %s(%s) to Bazel-remote', name, uid)
        if uid in self._meta or self.readonly():
            # should never happen
            logger.debug('Put %s(%s) to Bazel-remote completed(no-op)', name, uid)
            return True

        try:
            meta = self._client.put_data(files, root_dir, uid, name)
            self._meta[uid] = meta
        except BazelStoreException as e:
            logger.debug('Put %s(%s) to Bazel-remote failed: %s', name, uid, e)
            self._count_failure('put')
            return False

        data_size = self._get_data_size(meta)
        logger.debug('Put %s(%s) size=%d to Bazel-remote completed', name, uid, data_size)
        self._inc_data_size(data_size, 'put')
        return True

    def _do_try_restore(self, uid, into_dir, filter_func=None):
        try:
            meta = self._client.get_data(into_dir, uid, filter_func)
        except BazelStoreException:
            self._count_failure('get')
            return False

        data_size = self._get_data_size(meta)
        self._inc_data_size(data_size, 'get')
        return True

    @property
    def avg_compression_ratio(self):
        return 1.0
