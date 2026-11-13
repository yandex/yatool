import logging
import os
import requests
import sys
import time
from collections import namedtuple
from copy import deepcopy
from dataclasses import dataclass, field
from hashlib import md5

import exts.yjson as json
import yalibrary.fetcher.progress_info as progress_info_lib
from exts import fs
from library.python import func
from yalibrary import guards
from yalibrary import platform_matcher
from yalibrary import toolscache

from devtools.ya.core import config

from yalibrary.fetcher.uri_parser import parse_resource_uri

from .common import BINARY, stringify_memoize, ProgressPrinter, get_sandbox_token
from .cache_helper import (
    install_symlink,
    installed,
    safe_resource_lock,
)
from .resource_fetcher import (
    fetch_resource_if_need,
)

logger = logging.getLogger(__name__)


_SB_REST_API_TIMEOUT = 1 * 60
_RESERVED_ATTRIBUTES = frozenset(
    {
        "platform",
    }
)


class ResourceNotFound(RuntimeError):
    mute = True


@dataclass(kw_only=True, frozen=True)
class ExtraQuery:
    releases: str | None = None
    attributes: dict[str, str | None] = field(default_factory=dict)

    def __post_init__(self):
        for k in self.attributes:
            if k in _RESERVED_ATTRIBUTES:
                raise ValueError(f"Attribute {k} cannot be redefined")
        # Normalize key order for self.digest() and stringify_memoize()
        object.__setattr__(
            self,
            'attributes',
            dict(sorted(self.attributes.items())),
        )

    def __bool__(self):
        return self.releases is not None or bool(self.attributes)

    def digest(self) -> str:
        return md5(str(self).encode()).hexdigest()[:10]


@stringify_memoize
def get_tool_chain_fetcher(
    root,
    toolchain_name,
    bottle_name,
    formula,
    for_platform=None,
    binname=None,
    force_refetch=False,
    download_fn=fetch_resource_if_need,
    extra_query: ExtraQuery | None = None,
):
    if for_platform is None:
        for_platform = platform_matcher.my_platform()
    formula = get_formula_value(formula)
    platform_replacements = formula.get("platform_replacements")
    if "by_platform" in formula:
        return _ToolChainByPlatformFetcher(
            root,
            toolchain_name,
            platform_replacements,
            formula["by_platform"],
            for_platform,
            binname,
            force_refetch,
            download_fn,
        )
    elif "latest_matched" in formula:
        return _ToolChainLatestMatchedResourceFetcher(
            root,
            toolchain_name,
            platform_replacements,
            formula["latest_matched"],
            for_platform,
            binname,
            force_refetch,
            download_fn,
            bottle_name,
            extra_query,
        )
    else:
        raise Exception("Unsupported formula: {}".format(formula))


def resolve_resource_id(root, toolchain_name, bottle_name, formula, platform):
    return get_tool_chain_fetcher(root, toolchain_name, bottle_name, formula, platform).resource_id_from_cache()


def get_formula_value(formula):
    '''Return the value directly or read from a file if the value is a file name'''
    if isinstance(formula, str):
        return config.config_from_arc_rel_path(formula)
    else:
        return formula


def get_formula_platforms(formula):
    formula = get_formula_value(formula)
    if "by_platform" in formula:
        return list(formula["by_platform"].keys())
    else:
        return []


_ToolChainInfo = namedtuple('_ToolChainInfo', 'where')


class _ToolChainFetcherBase(object):
    def fetch_if_need(self):
        raise NotImplementedError()

    def resource_id_from_cache(self):
        raise NotImplementedError()


class _ToolChainFetcherImplBase(_ToolChainFetcherBase):
    def __init__(self, root, toolchain_name, platform_replacements, for_platform, binname, force_refetch, download_fn):
        self._root = root
        self._where = None
        self._toolchain_name = toolchain_name
        self._platform_replacements = platform_replacements
        self._platform = for_platform
        self._binname = binname
        self._binname_kwargs = dict(install_params=(BINARY, True), binname=binname) if binname else {}
        self._force_refetch = force_refetch
        self._download_fn = download_fn

    @stringify_memoize(cache_kwarg='cache')
    def fetch_if_need(self):
        if self._force_refetch or not self._check():
            logger.debug(
                "{0}: try to fetch by {1} for '{2}'".format(self._toolchain_name, self._details(), self._platform)
            )
            self._where = self._fetch()
            logger.debug("{0}: successfully fetched into {1}".format(self._toolchain_name, self._where))
        return _ToolChainInfo(self._where)

    def _check(self):
        if self._where is not None:
            toolscache.notify_tool_cache(self._where)
            return installed(self._where)
        return False

    def _fetch(self):
        raise NotImplementedError()

    def _details(self):
        raise NotImplementedError()


class _ToolChainByPlatformFetcher(_ToolChainFetcherImplBase):
    def __init__(
        self, root, name, platform_replacements, by_platform, for_platform, binname, force_refetch, download_fn
    ):
        super(_ToolChainByPlatformFetcher, self).__init__(
            root, name, platform_replacements, for_platform, binname, force_refetch, download_fn
        )
        self.__by_platform = by_platform

    def resource_id_from_cache(self):
        resource = self._get_matched_resource()
        return self._get_resource_uri(resource)

    def _get_matched_resource(self):
        best_match = platform_matcher.match_platform(
            self._platform, list(self.__by_platform.keys()), self._platform_replacements
        )
        if best_match is not None:
            logger.debug("{0}: will use '{1}' platform".format(self._toolchain_name, best_match))
            return self.__by_platform[best_match]
        else:
            raise platform_matcher.PlatformNotSupportedException(
                '{}: platform ({}) not matched to any of {}'.format(
                    self._toolchain_name, self._platform, self.__by_platform.keys()
                )
            )

    def _get_resource_uri(self, resource_desc):
        return resource_desc['uri']

    def _details(self):
        return self.__by_platform

    def _fetch(self):
        resource_desc = self._get_matched_resource()
        strip_prefix = resource_desc.get('strip_prefix')
        parsed_uri = parse_resource_uri(self._get_resource_uri(resource_desc))
        fetcher, progress_callback = _get_fetcher(self._toolchain_name, parsed_uri.resource_type)
        res = self._download_fn(
            fetcher,
            self._root,
            self._get_resource_uri(resource_desc),
            progress_callback,
            force_refetch=self._force_refetch,
            strip_prefix=strip_prefix,
            **self._binname_kwargs,
        )
        return res.where


class _ToolChainLatestMatchedResourceFetcher(_ToolChainFetcherImplBase):
    _DEFAULT_UPDATE_INTERVAL = 24 * 3600
    _DISABLE_AUTO_UPDATE_FILE_NAME = '.disable.auto.update'
    _updated_toolchains = set()

    def __init__(
        self,
        root,
        toolchain_name,
        platform_replacements,
        params,
        for_platform,
        binname,
        force_refetch,
        download_fn,
        bottle_name,
        extra_query: ExtraQuery | None = None,
    ):
        super(_ToolChainLatestMatchedResourceFetcher, self).__init__(
            root, toolchain_name, platform_replacements, for_platform, binname, force_refetch, download_fn
        )
        self.__params = params
        self.__bottle_name = bottle_name
        self.__update_interval = params.get('update_interval', self._DEFAULT_UPDATE_INTERVAL)
        self.__force_update = bool(os.environ.get('YA_TOOL_FORCE_UPDATE'))
        self.__extra_query_digest = extra_query.digest() if extra_query else None
        if params.get('ignore_platform', False):
            self.__platforms = []
        else:
            platforms = [for_platform] + platform_matcher.get_platform_replacements(
                for_platform, self._platform_replacements
            )
            self.__platforms = self._add_default_arch(platforms)
        if extra_query:
            # Patch query
            self.__params = deepcopy(self.__params)
            query = self.__params['query']
            if extra_query.releases is not None:
                if extra_query.releases == '':
                    query.pop('releases', None)
                else:
                    query['releases'] = extra_query.releases
            if extra_query.attributes:
                attrs = query.pop('attributes', {})
                for k, v in extra_query.attributes.items():
                    if v is None:
                        attrs.pop(k, None)
                    else:
                        attrs[k] = v
                if attrs:
                    query['attributes'] = attrs

    def _add_default_arch(self, platforms):
        default_arch = 'x86_64'
        result = []
        for p in platforms:
            if '-' not in p:
                result += [p, '{}-{}'.format(p, default_arch)]
            else:
                result.append(p)
        return result

    def resource_id_from_cache(self):
        resource_id = self._get_matched_resource_id()
        return 'sbr:{}'.format(resource_id)

    def _get_symlink_path(self):
        bottle_name = self.__bottle_name
        if self.__extra_query_digest:
            bottle_name += '@' + self.__extra_query_digest
        return os.path.join(self._root, '-'.join([bottle_name, self._platform]))

    def _get_info_path(self):
        return self._get_symlink_path() + '.info'

    def _write_info(self, info):
        info_path = self._get_info_path()
        tmp = info_path + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(info, f)
        fs.replace(tmp, info_path)
        logger.debug('{}: write info "{}" to {}'.format(self._toolchain_name, info, info_path))

    def _read_info(self):
        info_path = self._get_info_path()
        with open(info_path) as f:
            info = json.load(f)
        logger.debug('{}: info "{}" was read from {}'.format(self._toolchain_name, info, info_path))
        return info

    def _get_matched_resource_id(self):
        if config.has_mapping():
            return self._get_resource_id_from_mapping_config()

        resource_id = self._get_resource_id_from_info_file()
        if resource_id is None:
            link_path = self._get_symlink_path()
            # Use symlink path for lock file as install_symlink() does
            with safe_resource_lock(link_path):
                resource = self._find_resource()
                resource_id = resource['id']
                info = {
                    'update_time': int(time.time()),
                    'resource_id': resource_id,
                }
                self._write_info(info)
        return resource_id

    def _get_resource_id_from_mapping_config(self):
        mapping = config.mapping()
        bottle = mapping.get('bottles')
        if bottle is None:
            raise Exception("'bottles' section is missing in mapping config file")
        platforms = self.__platforms or ['any']
        for platform in platforms:
            if platform in bottle:
                return bottle[platform]
        raise ResourceNotFound('{}: no resource is found in bottle mapping: {}'.format(self._toolchain_name, bottle))

    def _get_resource_id_from_info_file(self):
        info_path = self._get_info_path()
        update_time_threshold = time.time() - self.__update_interval
        if os.path.exists(info_path):
            info = self._read_info()
            update_time = info['update_time']
            if (
                self.__force_update
                and self._toolchain_name not in _ToolChainLatestMatchedResourceFetcher._updated_toolchains
            ):
                logger.debug('{}: force update'.format(self._toolchain_name))
                _ToolChainLatestMatchedResourceFetcher._updated_toolchains.add(self._toolchain_name)
            elif update_time > update_time_threshold or self.__is_auto_update_disabled:
                resource_id = info['resource_id']
                logger.debug('{}: use cached toolchain with resource id: {}'.format(self._toolchain_name, resource_id))
                return resource_id
            else:
                logger.debug('{}: it is time to check toolchain for update'.format(self._toolchain_name))
        elif self.__is_auto_update_disabled:
            raise Exception('File {} is mandatory if auto update is disabled'.format(info_path))
        return None

    def _compact_res_info(self, res):
        # XXX 'links' field might be really huge
        limit = 10

        links = res.get('http', {}).get('links', [])
        if len(links) > limit:
            res = res.copy()
            res['http'] = res['http'].copy()
            res['http']['links'] = links[:limit] + ['<{} more links>'.format(len(links) - limit)]

        return res

    def _find_resource(self):
        failed_queries = []
        for sb_api_query in self._get_sb_api_queries():
            logger.debug('%s: lookup resources with query: %s', self._toolchain_name, sb_api_query)
            resources = _list_resources_no_links(**sb_api_query)
            if resources:
                assert len(resources) == 1
                resource = resources[0]
                logger.debug('%s: found resource: %s', self._toolchain_name, self._compact_res_info(resource))
                return resource
            failed_queries.append(json.dumps(sb_api_query))
        raise ResourceNotFound(
            '{}: no resource is found. Sandbox queries made:\n{}'.format(
                self._toolchain_name, ',\n'.join(failed_queries)
            )
        )

    def _get_sb_api_queries(self, platforms=None, limit=1):
        from sandbox.common.types.resource import State as ResourceState

        sb_api_query = {
            'type': self.__params['query']['resource_type'],
            'owner': self.__params['query']['owner'],
            'state': ResourceState.READY,
            'order': '-id',
            'limit': limit,
        }
        if 'releases' in self.__params['query']:
            sb_api_query['releases'] = self.__params['query']['releases']
        if 'attributes' in self.__params['query']:
            sb_api_query['attrs'] = self.__params['query']['attributes'].copy()
        if platforms is None:
            platforms = self.__platforms
        if platforms:
            for platform in self.__platforms:
                sb_api_query.setdefault('attrs', {})['platform'] = platform
                yield deepcopy(sb_api_query)
        else:
            yield sb_api_query

    def _get_resource_uri(self, resource_id):
        return "sbr:{}".format(resource_id)

    def _details(self):
        return "params: {}".format(self.__params)

    def _fetch(self):
        resource_id = self._get_matched_resource_id()
        fetcher, progress_callback = _get_fetcher(self._toolchain_name, 'sbr')
        self._download_fn(
            fetcher,
            self._root,
            self._get_resource_uri(resource_id),
            progress_callback,
            force_refetch=self._force_refetch,
            **self._binname_kwargs,
        )
        return self._install_symlink(resource_id)

    def _install_symlink(self, resource_id):
        resource_path = self._resource_path(resource_id)
        link_path = self._get_symlink_path()
        toolscache.notify_tool_cache(link_path)
        return install_symlink(resource_path, link_path)

    def _resource_path(self, resource_id):
        return os.path.join(self._root, str(resource_id))

    @func.lazy_property
    def __is_auto_update_disabled(self):
        return os.path.exists(os.path.join(self._root, self._DISABLE_AUTO_UPDATE_FILE_NAME))


def _get_fetcher(name, resource_type):
    def default_progress_callback(_, __):
        sys.stderr.write('.')
        sys.stderr.flush()

    def default_finish_callback():
        sys.stderr.write('\n')
        sys.stderr.flush()

    progress_callback = default_progress_callback
    finish_callback = default_finish_callback

    try:
        import app_ctx

        if getattr(app_ctx, 'state') and getattr(app_ctx, 'display'):
            progress_info = progress_info_lib.ProgressInfo()

            use_universal_fetcher_everywhere = (
                hasattr(app_ctx, 'use_universal_fetcher_everywhere') and app_ctx.use_universal_fetcher_everywhere
            )

            def display_progress(downloaded, total):
                if not use_universal_fetcher_everywhere:
                    app_ctx.state.check_cancel_state()

                progress_info.set_total(total)
                progress_info.update_downloaded(downloaded)

                app_ctx.display.emit_status(
                    'Downloading [[imp]]{}[[rst]] - [[imp]]{}[[rst]]'.format(name, progress_info.pretty_progress)
                )

            def display_finish():
                app_ctx.display.emit_status('')

            progress_callback = display_progress
            finish_callback = display_finish

        progress_printer = ProgressPrinter(
            progress_callback=progress_callback,
            finish_callback=finish_callback,
        )
        return app_ctx.legacy_sandbox_fetcher, progress_printer
    except (ImportError, AttributeError):
        progress_printer = ProgressPrinter(
            progress_callback=default_progress_callback, finish_callback=default_finish_callback
        )

    try:
        from devtools.ya.yalibrary.yandex.sandbox import fetcher

        return fetcher.SandboxFetcher(), progress_printer
    except ImportError:
        return None, progress_printer


def _list_resources_no_links(**kwargs):
    # A widely held resource has thousands of host links, and the lookup needs only its id
    from devtools.ya.yalibrary.yandex.sandbox import SandboxClient

    return _do_sandbox_method(SandboxClient.list_resources_no_links, **kwargs)


def _do_sandbox_method(method, **kwargs):
    rest_params = {'total_wait': _SB_REST_API_TIMEOUT, 'check_for_cancel': _get_cancel_checker()}
    guards.update_guard(guards.GuardTypes.FETCH)
    from devtools.ya.yalibrary.yandex.sandbox import SandboxClient

    try:
        return method(SandboxClient(token=get_sandbox_token(), rest_params=rest_params), **kwargs)
    except requests.HTTPError as e:
        logger.debug("Error connecting to sandbox: %s", e)
        if e.response.status_code != 403:
            raise e
    logger.warning("Incorrect sandbox token, trying anonymous access")

    return method(SandboxClient(rest_params=rest_params), **kwargs)


def _get_cancel_checker():
    try:
        import app_ctx

        return app_ctx.state.check_cancel_state
    except (ImportError, AttributeError):
        return None
