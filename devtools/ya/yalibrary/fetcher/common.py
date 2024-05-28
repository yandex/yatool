import logging
import os
import stat

from collections import namedtuple
from functools import wraps
from toolz.functoolz import curry, memoize

import exts.fs as fs
import exts.hashing
import exts.yjson as json

logger = logging.getLogger(__name__)

SUPPORTED_INTEGRITY_ALG = {'md5', 'sha1', 'sha512'}
UNTAR = 0
RENAME = 1
FIXED_NAME = 2
BINARY = 3


class InvalidHttpUriException(Exception):
    def __init__(self, reason, uri):  # type: (str, str) -> None
        msg = 'Wrong uri. {0}: {1}'.format(reason, uri)
        super(InvalidHttpUriException, self).__init__(msg)


def clean_dir(dir):
    try:
        os.unlink(dir)
    except OSError:
        pass
    fs.remove_tree_safe(dir)
    fs.create_dirs(dir)


def deploy_tool(archive, extract_to, post_process, resource_info, resource_uri, binname=None, strip_prefix=None):
    RESOURCE_INFO_JSON = "resource_info.json"
    RESOURCE_CONTENT_FILE_NAME = "resource"
    RESOURCE_URI = "lnk"

    if UNTAR == post_process:
        try:
            import exts.archive

            logger.debug("extract {0} to {1} dir (strip_prefix={2})".format(archive, extract_to, strip_prefix))
            exts.archive.extract_from_tar(archive, extract_to, strip_components=strip_prefix)
        finally:
            fs.remove_file(archive)
    elif RENAME == post_process:
        base_name = os.path.basename(resource_info['file_name'])
        logger.debug("move {0} to {1} dir".format(archive, os.path.join(extract_to, base_name)))
        fs.move(archive, os.path.join(extract_to, base_name))
    elif FIXED_NAME == post_process:
        logger.debug("move {0} to {1}".format(archive, os.path.join(extract_to, RESOURCE_CONTENT_FILE_NAME)))
        fs.move(archive, os.path.join(extract_to, RESOURCE_CONTENT_FILE_NAME))
    elif BINARY == post_process:
        file_name = os.path.basename(binname)
        full_path = os.path.join(extract_to, file_name)
        fs.replace(archive, full_path)
        st = os.stat(full_path)
        os.chmod(full_path, st.st_mode | stat.S_IEXEC)

    meta_info = os.path.join(extract_to, RESOURCE_INFO_JSON)
    if os.path.exists(meta_info):
        logger.debug("Meta information cannot be stored: {} already exists".format(meta_info))
    else:
        with open(meta_info, "w") as f:
            json.dump(resource_info, f, indent=4)

    uri_file = os.path.join(extract_to, RESOURCE_URI)
    if os.path.exists(uri_file):
        logger.debug("Link information cannot be stored: {} already exists".format(uri_file))
    else:
        with open(uri_file, "w") as f:
            f.write(resource_uri)


ParsedResourceUri = namedtuple(
    'ParsedResourceUri', 'resource_type, resource_uri, resource_id, resource_url, fetcher_meta'
)


def validate_parsed_http_meta(meta, resource_uri):  # type: (dict[str,str], str) -> None
    integrity = meta.get('integrity', None)
    if not integrity:
        raise InvalidHttpUriException('Integrity is required but not provided', resource_uri)
    if '-' not in integrity:
        raise InvalidHttpUriException('Integrity should be in form {alg}-{data}', resource_uri)
    alg, _ = integrity.split('-', 1)
    if alg not in SUPPORTED_INTEGRITY_ALG:
        algorithms = ", ".join(sorted(SUPPORTED_INTEGRITY_ALG))
        raise InvalidHttpUriException(
            'Integrity algorithm supports {0}, got \'{1}\''.format(algorithms, alg), resource_uri
        )


def parse_http_uri_meta(meta_str, resource_uri):  # type: (str,str) -> tuple[dict[str, str], str]
    pairs = meta_str.split('&')
    meta_dict = {}  # type: dict[str,str]
    for pair in pairs:
        key, value = pair.split('=', 1)
        meta_dict[key] = value.replace('%26', '&')

    validate_parsed_http_meta(meta_dict, resource_uri)
    resource_id = exts.hashing.md5_value(meta_dict.get('integrity'))
    return meta_dict, resource_id


def parse_http_uri(resource_uri):  # type: (str) -> ParsedResourceUri
    if '#' not in resource_uri:
        raise InvalidHttpUriException('No \'#\' symbol is found', resource_uri)

    resource_url, meta_str = resource_uri.split('#', 1)

    if not meta_str:
        raise InvalidHttpUriException('\'#\' symbol must be followed by resource meta', resource_uri)

    if '=' not in meta_str:
        # backward compatibility with old md5-scheme
        return ParsedResourceUri('http', resource_uri, meta_str, resource_url, fetcher_meta=None)

    meta, resource_id = parse_http_uri_meta(meta_str, resource_uri)
    return ParsedResourceUri('http', resource_uri, resource_id, resource_url, fetcher_meta=meta)


def parse_resource_uri(resource_uri, force_accepted_schemas=None):  # type: (str, set[str]) -> ParsedResourceUri
    if not force_accepted_schemas:
        try:
            import app_ctx

            accepted_schemas = app_ctx.fetchers_storage.accepted_schemas()
        except ImportError:  # internal tests have no app_ctx
            accepted_schemas = {'sbr'}
    else:
        accepted_schemas = force_accepted_schemas

    resource_type, rest = resource_uri.split(':', 1)
    if resource_type in ('http', 'https'):
        return parse_http_uri(resource_uri)
    elif resource_type == 'base64':
        return ParsedResourceUri(resource_type, resource_uri, resource_id=None, resource_url=rest, fetcher_meta=None)
    elif resource_type in accepted_schemas:
        resource_id = rest
        return ParsedResourceUri(resource_type, resource_uri, resource_id, resource_url=None, fetcher_meta=None)
    else:
        raise Exception('Unknown platform in uri: {}'.format(resource_uri))


@curry
def stringify_memoize(orig_func, cache_kwarg=None):
    '''
    Creative rethinking of pg's caching approach.

    Memoize a function using it's parameters stringification as a key.
    If cache_kwarg is not None it's value is used as a name for additional kwarg.
    Passing this kwarg with False value disable memoization for the particular call.
    Notice: this kwarg will be never passed to the original function.
    '''
    memoized_func = memoize(func=orig_func, key=lambda args, kwargs: str((args, list(sorted(kwargs)))))
    if cache_kwarg is None:
        return memoized_func

    @wraps(orig_func)
    def wrapper(*args, **kwargs):
        if kwargs.pop(cache_kwarg, True):
            return memoized_func(*args, **kwargs)
        else:
            return orig_func(*args, **kwargs)

    return wrapper


class ProgressPrinter(object):
    def __init__(self, progress_callback, finish_callback=lambda: None):
        self._progress = progress_callback
        self._finish = finish_callback

    def __call__(self, percent):
        self._progress(percent)

    def finalize(self):
        self._finish()
