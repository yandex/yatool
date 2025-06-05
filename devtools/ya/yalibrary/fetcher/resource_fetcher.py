import base64
import logging
import os
import sys
import collections

from exts import uniq_id, http_client
from exts.hashing import md5_value
from yalibrary import guards
from yalibrary import platform_matcher
from devtools.libs.yaplatform.python.platform_map import get_resource_dir_name

from devtools.ya.core import config

from yalibrary.fetcher.uri_parser import parse_resource_uri, get_mapped_parsed_uri_and_info

from .common import RENAME, UNTAR, ProgressPrinter, clean_dir, deploy_tool

from .cache_helper import install_resource


class MissingResourceError(Exception):
    mute = True


FetchResponse = collections.namedtuple("FetchResponse", ("install_stat", "where"))

CAN_USE_UNIVERSAL_FETCHER = sys.version_info[0] >= 3
FALLBACK_MSG_LOGGED = False


logger = logging.getLogger(__name__)


def fetch_base64_resource(root_dir, resource_uri):
    parsed_uri = parse_resource_uri(resource_uri)
    assert parsed_uri.resource_type == 'base64'

    base_dir = md5_value(parsed_uri.resource_url)
    result_dir = os.path.join(root_dir, base_dir)

    logger.debug("Fetching {} to {} dir)".format(parsed_uri.resource_url[:20], result_dir))

    def do_download(download_to):
        base_name, content = parsed_uri.resource_url.split(':', 1)
        with open(os.path.join(download_to), 'wb') as f:
            f.write(base64.b64decode(content))
        return {"file_name": base_name}

    def do_deploy(download_to, resource_info):
        deploy_tool(download_to, result_dir, RENAME, resource_info, resource_uri)

    return _do_fetch_resource_if_need(result_dir, do_download, do_deploy, target_is_tool_dir=False)


# install_params are
#   1. post-process:
#       - UNTAR - unpack archive from resource
#       - RENAME - rename according to file_name from resource_info
#       - FIXED_NAME - keep resource under fixed name 'resource'
#   2. tool_dir (True) or resource_dir (False)
def fetch_resource_if_need(
    fetcher,
    root_dir,
    resource_uri,
    progress_callback=lambda _, __: None,
    state=None,
    install_params=(UNTAR, True),
    binname=None,
    force_refetch=False,
    keep_directory_packed=False,
    strip_prefix=None,
    force_universal_fetcher=True,
):
    global FALLBACK_MSG_LOGGED

    use_universal_fetcher = force_universal_fetcher
    if not use_universal_fetcher:
        try:
            import app_ctx

            use_universal_fetcher = app_ctx.use_universal_fetcher_everywhere
        except (ImportError, AttributeError):
            pass

    if use_universal_fetcher and not CAN_USE_UNIVERSAL_FETCHER:
        if not FALLBACK_MSG_LOGGED:
            FALLBACK_MSG_LOGGED = True
            logger.debug("Can't use universal fetcher. Fallback to default mode.")

        use_universal_fetcher = False

    parsed_uri = parse_resource_uri(resource_uri)
    post_process, target_is_tool_dir = install_params
    resource_dir = get_resource_dir_name(resource_uri, strip_prefix)
    result_dir = os.path.join(root_dir, resource_dir)

    logger.debug(
        "Fetching %s from %s to %s dir, post_process=%s)"
        % (parsed_uri.resource_id, parsed_uri.resource_uri, result_dir, post_process)
    )

    downloader = _get_downloader(
        fetcher, parsed_uri, progress_callback, state, keep_directory_packed, use_universal_fetcher
    )

    def do_deploy(download_to, resource_info):
        deploy_tool(download_to, result_dir, post_process, resource_info, resource_uri, binname, strip_prefix)

    return _do_fetch_resource_if_need(
        result_dir,
        downloader,
        do_deploy,
        target_is_tool_dir,
        force_refetch,
    )


def select_resource(item, platform=None):
    if 'resource' in item:
        return item
    elif 'resources' in item:
        if platform:
            try:
                parsed = platform_matcher.parse_platform(platform)
                platform = platform_matcher.canonize_full_platform('-'.join((parsed['os'], parsed['arch']))).lower()
            except platform_matcher.InvalidPlatformSpecification:
                platform = platform_matcher.canonize_full_platform(platform).lower()
        else:
            platform = platform_matcher.my_platform().lower()
        for res in item['resources']:
            if res['platform'].lower() == platform:
                return res
        raise Exception('Unable to find resource {} for current platform'.format(item['pattern']))
    else:
        raise Exception('Incorrect resource format')


def _get_downloader(fetcher, parsed_uri, progress_callback, state, keep_directory_packed, use_universal_fetcher):
    resource_info = {
        'file_name': parsed_uri.resource_id[:20],
        'id': parsed_uri.resource_id,
    }

    if config.has_mapping():
        parsed_uri, resource_info = get_mapped_parsed_uri_and_info(parsed_uri, config.mapping(), resource_info)

    if use_universal_fetcher:
        import yalibrary.fetcher.ufetcher as ufetcher

        # TODO: kuzmich321 (ufetcher) seems like there can be a better way?
        what_to_download = parsed_uri.resource_url or parsed_uri.resource_uri

        return ufetcher.UFetcherDownloader(
            ufetcher.get_ufetcher(),
            what_to_download,
            progress_callback,
            keep_directory_packed,
            resource_info if config.has_mapping() or parsed_uri.resource_type in ('http', 'docker') else None,
            parsed_uri.resource_type,
        )

    if parsed_uri.resource_type == 'http':
        if parsed_uri.fetcher_meta:
            resource_info['file_name'] = 'resource'
            integrity = parsed_uri.fetcher_meta.get('integrity')
            return _HttpDownloaderWithIntegrity(parsed_uri.resource_url, integrity, resource_info)
        return _HttpDownloader(parsed_uri.resource_url, parsed_uri.resource_id, resource_info)

    elif parsed_uri.resource_type in frozenset(['sbr', 'http', 'https']):
        if config.has_mapping():
            return _HttpDownloader(parsed_uri.resource_url, None, resource_info)
        return _DefaultDownloader(fetcher, parsed_uri.resource_id, progress_callback, state, keep_directory_packed)

    raise Exception('Unsupported resource_uri {}'.format(parsed_uri.resource_uri))


def _do_fetch_resource_if_need(
    result_dir,
    downloader,
    deployer,
    target_is_tool_dir=True,
    force_refetch=False,
):
    def do_install():
        guards.update_guard(guards.GuardTypes.FETCH)
        filename = 'resource.' + uniq_id.gen8()
        download_to = os.path.join(result_dir, filename)
        resource_info = downloader(download_to)
        deployer(download_to, resource_info)
        return resource_info

    if target_is_tool_dir:
        install_stat, where = install_resource(result_dir, do_install, force_refetch)
        return FetchResponse(install_stat, where)
    else:
        clean_dir(result_dir)
        install_stat = do_install()
        return FetchResponse(install_stat, result_dir)


class DownloaderBase(object):
    def __call__(self, download_to):
        raise NotImplementedError()


class _HttpDownloader(DownloaderBase):
    def __init__(self, resource_url, resource_md5, resource_info):
        self._url = resource_url
        self._md5 = resource_md5
        self._info = resource_info

    def __call__(self, download_to):
        http_client.download_file(url=self._url, path=download_to, expected_md5=self._md5)
        return self._info


class _HttpDownloaderWithIntegrity(DownloaderBase):
    def __init__(self, resource_url, integrity, resource_info):
        # type: (str, str, dict[str,str]) -> None
        self._url = resource_url
        self._integrity = integrity
        self._info = resource_info

    def __call__(self, download_to):
        http_client.download_file_with_integrity(url=self._url, path=download_to, integrity=self._integrity)
        return self._info


class _DefaultDownloader(DownloaderBase):
    def __init__(self, fetcher, resource_id, progress_callback, state, keep_directory_packed):
        self._fetcher = fetcher
        self._resource_id = resource_id
        self._progress_callback = progress_callback
        self._state = state
        self._keep_directory_packed = keep_directory_packed

    def __call__(self, download_to):
        try:
            return self._fetcher.fetch_resource(
                self._resource_id, download_to, self._progress_callback, self._state, self._keep_directory_packed
            )
        finally:
            if isinstance(self._progress_callback, ProgressPrinter):
                self._progress_callback.finalize()
